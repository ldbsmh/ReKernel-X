#include <jni.h>
#include <atomic>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <algorithm>

#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>


#include "rekernel_x_uapi.h"
#include "rekernel_x_nla.h"
#include "rekernel_x_log.h"

/* ==========================================================================
 * Global JNI state
 * ========================================================================== */

static jclass g_cbClass = nullptr;
static jmethodID g_mid_binder = nullptr;
static jmethodID g_mid_signal = nullptr;
static jmethodID g_mid_network = nullptr;

static jobject g_callback = nullptr;
static std::mutex g_callback_mutex;

static std::atomic<int> g_fd{-1};
static std::atomic<int> g_disconnect_fd{-1};
static std::atomic<uint16_t> g_family_id{0};
static uint32_t g_mcast_group_id = 0;
static std::atomic<uint32_t> g_seq{1};

/* ==========================================================================
 * JNI Lifecycle: OnLoad & OnUnload
 * ========================================================================== */

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    jclass cls = env->FindClass("cn/myflv/kernel/ReKernelXCallback");
    if (!cls) {
        return JNI_ERR;
    }
    g_cbClass = reinterpret_cast<jclass>(env->NewGlobalRef(cls));
    env->DeleteLocalRef(cls);

    g_mid_binder = env->GetMethodID(g_cbClass, "binder", "(IIIIIILjava/lang/String;I)V");
    g_mid_signal = env->GetMethodID(g_cbClass, "signal", "(IIIII)V");
    g_mid_network = env->GetMethodID(g_cbClass, "network", "(III)V");

    if (!g_mid_binder || !g_mid_signal || !g_mid_network) {
        return JNI_ERR;
    }

    rekernel_x_log_info("callback class loaded");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved) {
    JNIEnv *env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
        if (g_cbClass) {
            env->DeleteGlobalRef(g_cbClass);
            g_cbClass = nullptr;
        }
    }
    rekernel_x_log_info("callback class released");
}

/* ==========================================================================
 * Netlink socket helpers
 * ========================================================================== */

static int nl_sendto(int fd, const void *buf, size_t len) {
    struct sockaddr_nl dst = {};
    dst.nl_family = AF_NETLINK;
    ssize_t n = sendto(fd, buf, len, 0, reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    return (n == static_cast<ssize_t>(len)) ? 0 : -1;
}

static int nl_recv(int fd, void *buf, size_t bufsize) {
    struct sockaddr_nl src = {};
    socklen_t addrlen = sizeof(src);
    ssize_t n = recvfrom(fd, buf, bufsize, 0, reinterpret_cast<struct sockaddr *>(&src), &addrlen);
    return static_cast<int>(n);
}

static uint32_t find_mcast_group_id(struct nlattr *mcast_attr, const char *target_name) {
    struct nlattr *grp_nla;
    int grp_rem;
    nla_for_each_nested(grp_nla, mcast_attr, grp_rem) {
        struct nlattr *field_nla;
        int field_rem;
        char grp_name[64] = {};
        uint32_t grp_id = 0;
        bool have_name = false, have_id = false;

        nla_for_each_nested(field_nla, grp_nla, field_rem) {
            int type = nla_type(field_nla);
            if (type == CTRL_ATTR_MCAST_GRP_NAME && nla_len(field_nla) > 0) {
                int slen = nla_len(field_nla);
                if (slen > 63) {
                    slen = 63;
                }
                memcpy(grp_name, nla_data(field_nla), slen);
                grp_name[slen] = '\0';
                have_name = true;
            } else if (type == CTRL_ATTR_MCAST_GRP_ID) {
                nla_get(field_nla, grp_id);
                have_id = true;
            }
        }
        if (have_name && have_id && strcmp(grp_name, target_name) == 0) {
            return grp_id;
        }
    }
    return 0;
}

static int resolveFamily_locked() {
    const char *name = REKERNEL_X_GENL_FAMILY_NAME;
    auto name_len = static_cast<uint16_t>(strlen(name) + 1);
    uint16_t attr_len = NLA_HDRLEN + name_len;
    uint16_t total = NLMSG_HDRLEN + GENL_HDRLEN + NLA_ALIGN(attr_len);

    uint8_t buf[256] = {};
    auto *nlh = reinterpret_cast<struct nlmsghdr *>(buf);
    nlh->nlmsg_len = total;
    nlh->nlmsg_type = GENL_ID_CTRL;
    nlh->nlmsg_flags = NLM_F_REQUEST;
    nlh->nlmsg_seq = g_seq.fetch_add(1);

    auto *ghdr = reinterpret_cast<struct genlmsghdr *>(buf + NLMSG_HDRLEN);
    ghdr->cmd = CTRL_CMD_GETFAMILY;
    ghdr->version = 1;

    auto *nla = reinterpret_cast<struct nlattr *>(buf + NLMSG_HDRLEN + GENL_HDRLEN);
    nla->nla_len = attr_len;
    nla->nla_type = CTRL_ATTR_FAMILY_NAME;
    memcpy(nla_data(nla), name, name_len);

    int fd = g_fd.load();
    struct timeval rcvto = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvto, sizeof(rcvto));

    if (nl_sendto(fd, buf, total) < 0) {
        return -1;
    }

    uint8_t reply[4096];
    int rlen = nl_recv(fd, reply, sizeof(reply));
    if (rlen < static_cast<int>(NLMSG_HDRLEN)) {
        return -1;
    }

    auto *rnlh = reinterpret_cast<struct nlmsghdr *>(reply);
    if (rnlh->nlmsg_type == NLMSG_ERROR) {
        return -1;
    }

    uint16_t fid = 0;
    uint32_t gid = 0;
    bool have_fid = false;

    int attr_off = NLMSG_HDRLEN + GENL_HDRLEN;
    int attr_end = std::min(static_cast<int>(rnlh->nlmsg_len), rlen);

    struct nlattr *attr_nla;
    int attr_rem;
    nla_for_each_attr(attr_nla, reply + attr_off, attr_end - attr_off, attr_rem) {
        int type = nla_type(attr_nla);
        if (type == CTRL_ATTR_FAMILY_ID) {
            nla_get(attr_nla, fid);
            have_fid = true;
        } else if (type == CTRL_ATTR_MCAST_GROUPS) {
            gid = find_mcast_group_id(attr_nla, REKERNEL_X_GENL_MCGRP_NAME);
        }
    }

    if (!have_fid || gid == 0) {
        return -1;
    }

    g_family_id.store(fid);
    g_mcast_group_id = gid;
    rekernel_x_log_info("family_id=%u mcast_group_id=%u", fid, gid);
    return 0;
}

/* ==========================================================================
 * Event dispatch
 * ========================================================================== */

static void handle_binder_event(JNIEnv *env, jobject cb, struct nlattr *payload) {
    int32_t binder_type = -1, oneway = -1, from_pid = -1, from_uid = -1;
    int32_t target_pid = -1, target_uid = -1, code = -1;
    char rpc_name[REKERNEL_X_RPC_NAME_LEN + 1] = {};

    struct nlattr *attr;
    int rem;
    nla_for_each_nested(attr, payload, rem) {
        switch (nla_type(attr)) {
            case REKERNEL_X_A_BINDER_TYPE:
                nla_get(attr, binder_type);
                break;
            case REKERNEL_X_A_BINDER_ONEWAY:
                nla_get(attr, oneway);
                break;
            case REKERNEL_X_A_BINDER_FROM_PID:
                nla_get(attr, from_pid);
                break;
            case REKERNEL_X_A_BINDER_FROM_UID:
                nla_get(attr, from_uid);
                break;
            case REKERNEL_X_A_BINDER_TARGET_PID:
                nla_get(attr, target_pid);
                break;
            case REKERNEL_X_A_BINDER_TARGET_UID:
                nla_get(attr, target_uid);
                break;
            case REKERNEL_X_A_BINDER_CODE:
                nla_get(attr, code);
                break;
            case REKERNEL_X_A_BINDER_RPC_NAME: {
                int slen = std::min((int) nla_len(attr), (int) REKERNEL_X_RPC_NAME_LEN);
                memcpy(rpc_name, nla_data(attr), slen);
                break;
            }
        }
    }

    jstring jRpcName = env->NewStringUTF(rpc_name);
    env->CallVoidMethod(cb, g_mid_binder,
                        binder_type, oneway, from_uid, from_pid,
                        target_uid, target_pid, jRpcName, code);

    if (jRpcName) {
        env->DeleteLocalRef(jRpcName);
    }
}

static void handle_signal_event(JNIEnv *env, jobject cb, struct nlattr *payload) {
    int32_t sig = -1, killer_uid = -1, killer_pid = -1, dst_uid = -1, dst_pid = -1;

    struct nlattr *attr;
    int rem;
    nla_for_each_nested(attr, payload, rem) {
        switch (nla_type(attr)) {
            case REKERNEL_X_A_SIGNAL_SIGNAL:
                nla_get(attr, sig);
                break;
            case REKERNEL_X_A_SIGNAL_KILLER_UID:
                nla_get(attr, killer_uid);
                break;
            case REKERNEL_X_A_SIGNAL_KILLER_PID:
                nla_get(attr, killer_pid);
                break;
            case REKERNEL_X_A_SIGNAL_DST_UID:
                nla_get(attr, dst_uid);
                break;
            case REKERNEL_X_A_SIGNAL_DST_PID:
                nla_get(attr, dst_pid);
                break;
        }
    }

    env->CallVoidMethod(cb, g_mid_signal, sig, killer_uid, killer_pid, dst_uid, dst_pid);
}

static void handle_network_event(JNIEnv *env, jobject cb, struct nlattr *payload) {
    int32_t proto = -1, target_uid = -1, data_len = -1;

    struct nlattr *attr;
    int rem;
    nla_for_each_nested(attr, payload, rem) {
        switch (nla_type(attr)) {
            case REKERNEL_X_A_NETWORK_PROTO:
                nla_get(attr, proto);
                break;
            case REKERNEL_X_A_NETWORK_TARGET_UID:
                nla_get(attr, target_uid);
                break;
            case REKERNEL_X_A_NETWORK_DATA_LEN:
                nla_get(attr, data_len);
                break;
        }
    }

    env->CallVoidMethod(cb, g_mid_network, proto, target_uid, data_len);
}

static void handle_event(JNIEnv *env, jobject cb, struct nlmsghdr *nlh) {
    if (nlh->nlmsg_type == NLMSG_ERROR || nlh->nlmsg_type == NLMSG_DONE) {
        return;
    }
    if (nlh->nlmsg_type != g_family_id.load()) {
        return;
    }
    if (nlh->nlmsg_len < NLMSG_HDRLEN + GENL_HDRLEN) {
        return;
    }

    auto *ghdr = reinterpret_cast<struct genlmsghdr *>
    (reinterpret_cast<uint8_t *>(nlh) + NLMSG_HDRLEN);

    if (ghdr->cmd != REKERNEL_X_C_EVENT) {
        return;
    }

    int attr_off = NLMSG_HDRLEN + GENL_HDRLEN;
    int attr_len = static_cast<int>(nlh->nlmsg_len) - attr_off;
    void *attr_data = reinterpret_cast<uint8_t *>(nlh) + attr_off;

    struct nlattr *evt_nla = nullptr;
    struct nlattr *attr;
    int rem;

    nla_for_each_attr(attr, attr_data, attr_len, rem) {
        if (nla_type(attr) == REKERNEL_X_A_EVENT) {
            evt_nla = attr;
            break;
        }
    }

    if (!evt_nla || nla_len(evt_nla) < NLA_HDRLEN) {
        return;
    }

    nla_for_each_nested(attr, evt_nla, rem) {
        switch (nla_type(attr)) {
            case REKERNEL_X_A_BINDER:
                handle_binder_event(env, cb, attr);
                break;
            case REKERNEL_X_A_SIGNAL:
                handle_signal_event(env, cb, attr);
                break;
            case REKERNEL_X_A_NETWORK:
                handle_network_event(env, cb, attr);
                break;
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

/* ==========================================================================
 * Exported JNI Interfaces
 * ========================================================================== */

extern "C" JNIEXPORT void JNICALL
Java_cn_myflv_kernel_ReKernelX_setCallback(JNIEnv *env, jclass clazz, jobject callback) {
    jobject new_cb = nullptr;

    if (callback) {
        if (!env->IsInstanceOf(callback, g_cbClass)) {
            rekernel_x_log_err("setCallback: Callback object does not implement ReKernelXCallback");
            return;
        }
        new_cb = env->NewGlobalRef(callback);
    }

    jobject old_cb = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_callback_mutex);
        old_cb = g_callback;
        g_callback = new_cb;
    }

    if (old_cb) {
        env->DeleteGlobalRef(old_cb);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cn_myflv_kernel_ReKernelX_connect(JNIEnv *env, jclass clazz) {
    if (g_fd.load() >= 0) {
        return JNI_TRUE;
    }

    int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC);
    if (fd < 0) {
        return JNI_FALSE;
    }

    int disconnect_fd = eventfd(0, EFD_CLOEXEC);
    if (disconnect_fd < 0) {
        close(fd);
        return JNI_FALSE;
    }

    int rcvbuf = 64 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    struct sockaddr_nl local = {};
    local.nl_family = AF_NETLINK;
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&local), sizeof(local)) < 0) {
        close(fd);
        close(disconnect_fd);
        return JNI_FALSE;
    }

    g_fd.store(fd);

    if (resolveFamily_locked() < 0) {
        g_fd.store(-1);
        close(fd);
        close(disconnect_fd);
        return JNI_FALSE;
    }

    struct timeval blockto = {0, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &blockto, sizeof(blockto));

    if (g_mcast_group_id > 0) {
        if (setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP,
                       &g_mcast_group_id, sizeof(g_mcast_group_id)) < 0) {
            g_fd.store(-1);
            close(fd);
            close(disconnect_fd);
            return JNI_FALSE;
        }
    }

    g_disconnect_fd.store(disconnect_fd);
    rekernel_x_log_info("connect: connected (family_id=%u)", g_family_id.load());
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_cn_myflv_kernel_ReKernelX_pollEvent(JNIEnv *env, jclass clazz) {
    uint8_t rbuf[8192];

    for (;;) {
        int fd = g_fd.load();
        int disconnect_fd = g_disconnect_fd.load();
        if (fd < 0 || disconnect_fd < 0) {
            break;
        }

        struct pollfd p_fds[2] = {
                {disconnect_fd, POLLIN, 0},
                {fd,            POLLIN, 0},
        };

        if (poll(p_fds, 2, -1) < 0) {
            if (errno == ENOMEM) {
                rekernel_x_log_err("pollEvent: poll ENOMEM");
            }
            continue;
        }

        if (p_fds[0].revents & POLLIN) {
            uint64_t v;
            read(disconnect_fd, &v, sizeof(v));
            break;
        }

        if (!(p_fds[1].revents & (POLLIN | POLLERR))) {
            continue;
        }

        int rlen = nl_recv(fd, rbuf, sizeof(rbuf));
        if (rlen <= 0) {
            continue;
        }

        jobject local_cb = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_callback_mutex);
            if (g_callback) {
                local_cb = env->NewLocalRef(g_callback);
            }
        }

        if (local_cb) {
            for (auto *nlh = reinterpret_cast<struct nlmsghdr *>(rbuf);
                 NLMSG_OK(nlh, static_cast<size_t>(rlen));
                 nlh = NLMSG_NEXT(nlh, rlen)) {
                handle_event(env, local_cb, nlh);
            }
            env->DeleteLocalRef(local_cb);
        }
    }

    int fd = g_fd.exchange(-1);
    int disconnect_fd = g_disconnect_fd.exchange(-1);

    if (fd >= 0) {
        close(fd);
    }

    if (disconnect_fd >= 0) {
        close(disconnect_fd);
    }

    rekernel_x_log_info("pollEvent: disconnected, returning");
}

extern "C" JNIEXPORT void JNICALL
Java_cn_myflv_kernel_ReKernelX_disconnect(JNIEnv *env, jclass clazz) {
    int efd = g_disconnect_fd.load();
    if (efd >= 0) {
        uint64_t v = 1;
        write(efd, &v, sizeof(v));
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cn_myflv_kernel_ReKernelX_addMonitorNet(JNIEnv *env, jclass clazz, jint uid) {
    uint16_t family = g_family_id.load();
    if (family == 0) {
        return JNI_FALSE;
    }

    int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC);
    if (fd < 0) {
        return JNI_FALSE;
    }

    uint8_t buf[128];
    NlaBuilder b(buf, sizeof(buf));
    b.putGenlHeader(family, g_seq.fetch_add(1),
                    REKERNEL_X_C_ADD_MONITOR_NET, REKERNEL_X_GENL_VERSION);
    b.putU32(REKERNEL_X_A_UID, static_cast<uint32_t>(uid));
    size_t len = b.finish();

    struct sockaddr_nl dst = {};
    dst.nl_family = AF_NETLINK;
    ssize_t n = sendto(fd, buf, len, 0, reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    close(fd);
    return (n == static_cast<ssize_t>(len)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_cn_myflv_kernel_ReKernelX_delMonitorNet(JNIEnv *env, jclass clazz, jint uid) {
    uint16_t family = g_family_id.load();
    if (family == 0) {
        return JNI_FALSE;
    }

    int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC);
    if (fd < 0) {
        return JNI_FALSE;
    }

    uint8_t buf[128];
    NlaBuilder b(buf, sizeof(buf));
    b.putGenlHeader(family, g_seq.fetch_add(1),
                    REKERNEL_X_C_DEL_MONITOR_NET, REKERNEL_X_GENL_VERSION);
    b.putU32(REKERNEL_X_A_UID, static_cast<uint32_t>(uid));
    size_t len = b.finish();

    struct sockaddr_nl dst = {};
    dst.nl_family = AF_NETLINK;
    ssize_t n = sendto(fd, buf, len, 0, reinterpret_cast<struct sockaddr *>(&dst), sizeof(dst));
    close(fd);
    return (n == static_cast<ssize_t>(len)) ? JNI_TRUE : JNI_FALSE;
}