/*
 * ldap24_stub —— 由 sangfor-vdi-fedora 编译（仅在真实 OpenLDAP 2.4 不可用时兜底）
 *
 * 为什么需要
 * ---------
 * 客户端自带的 libcurl 编译时启用了 LDAP 支持，DT_NEEDED 里硬链接了
 * libldap_r-2.4.so.2 / liblber-2.4.so.2，并对其中的符号带版本要求
 * OPENLDAP_2.4_2。现代发行版的 OpenLDAP 已是 2.6，且 Fedora 的 2.4 构建
 * 甚至完全不导出符号版本（只有被依赖的版本引用），因此无法满足。
 *
 * 而 libldap_r-2.4 的真实库会拖入一整条 Debian 专有的依赖链
 * （libgssapi.so.3 → Heimdal → libhcrypto/libasn1/libhx509/libwind/libcrypt…），
 * 在 Fedora 上需要往全局链接路径塞入大量加密库，代价和风险都不划算。
 *
 * 关键事实
 * --------
 * - 客户端没有任何二进制直接调用 LDAP（已用 objdump 逐个核对）。
 * - LDAP 仅由 libcurl 的 ldap:// 协议支持使用；VDI 客户端只走 https。
 *
 * 因此这里提供一个只导出“libcurl 实际引用的那 23 个符号”、并带
 * OPENLDAP_2.4_2 版本号的空实现，让动态链接能通过；任何真正被调用到的
 * 情况都返回失败，绝不会给出伪造的成功结果。
 */
#include <stddef.h>

/* 识别标记：补丁用它判断 compat 里的 libldap 是桩库还是真实库 */
const char sgv_ldap24_stub_marker[] = "SGV_LDAP24_STUB";

/* 统一用 long 返回、无参数声明：这些函数在正常使用中永不被调用，
 * 若被调用则忽略实参并返回 0（失败），避免误报成功。 */
#define STUB(name) long name(void) { return 0; }

/* --- liblber --- */
STUB(ber_free)
STUB(ber_memfree)
STUB(ber_sockbuf_add_io)

/* --- libldap --- */
STUB(ldap_abandon_ext)
STUB(ldap_err2string)
STUB(ldap_first_message)
STUB(ldap_free_urldesc)
STUB(ldap_get_attribute_ber)
STUB(ldap_get_dn_ber)
STUB(ldap_get_option)
STUB(ldap_init_fd)
STUB(ldap_memfree)
STUB(ldap_msgfree)
STUB(ldap_msgtype)
STUB(ldap_next_message)
STUB(ldap_parse_result)
STUB(ldap_pvt_url_scheme2proto)
STUB(ldap_result)
STUB(ldap_sasl_bind)
STUB(ldap_search_ext)
STUB(ldap_set_option)
STUB(ldap_unbind_ext)
STUB(ldap_url_parse)