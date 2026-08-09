#pragma once

/* 契约版本。规则见 docs/VERSIONING.md §3
 *   MAJOR：删除/改签名/改语义 —— 所有板必须同步修改
 *   MINOR：新增可选接口 —— 旧板不改也能编过
 */
#define BSP_API_VERSION_MAJOR   1
#define BSP_API_VERSION_MINOR   0

#include "bsp_caps.h"   /* 由 -I boards/<id> 提供 */

#if !defined(BSP_BOARD_ID) || !defined(BSP_BOARD_API_MAJOR)
#  error "boards/<id>/bsp_caps.h 缺少 BSP_BOARD_ID / BSP_BOARD_API_MAJOR 声明"
#endif

#if BSP_BOARD_API_MAJOR != BSP_API_VERSION_MAJOR
#  error "板级 BSP 与契约层主版本不匹配，请按 docs/VERSIONING.md §3.3 升级该板"
#endif

/* 版本串一律用这个宏拼装，各板不得手写字面量 —— 手写必然与 bsp_caps.h 漂移。
 * 形如 "lcd_085 v0.9.0 (draft) / api 1.0 / built Aug  5 2026" */
#define BSP_STR_HELPER_(x)  #x
#define BSP_STR_(x)         BSP_STR_HELPER_(x)

#define BSP_VERSION_STRING                                                     \
    BSP_BOARD_ID " v"                                                          \
    BSP_STR_(BSP_BOARD_VER_MAJOR) "."                                          \
    BSP_STR_(BSP_BOARD_VER_MINOR) "."                                          \
    BSP_STR_(BSP_BOARD_VER_PATCH)                                              \
    " (" BSP_BOARD_MATURITY ") / api "                                         \
    BSP_STR_(BSP_API_VERSION_MAJOR) "." BSP_STR_(BSP_API_VERSION_MINOR)        \
    " / built " __DATE__

#ifdef __cplusplus
extern "C" {
#endif

/* 供自检打印，让每一份串口日志都能自证来自哪块板的哪个版本。
 * 实现一律为 `return BSP_VERSION_STRING;` */
const char* bsp_version_string(void);

#ifdef __cplusplus
}
#endif
