/**
 * @file quicheCommon.h
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-05-06
 * @brief Common functions used by both server and client
/*********** (C) COPYRIGHT 2021 Victor ***********/

/**
 * @file quicheCommon.h
 * @version 1.0
 * @author Victor(1294958142@qq.com)
 * @date 2021-05-06
 * @brief
/*********** (C) COPYRIGHT 2021 Victor ***********/

#ifndef _QUICHECOMMON_H_INCLUDED_
#define _QUICHECOMMON_H_INCLUDED_

#include <blink/quicheConfig.h>

namespace quiche {

void flush_egress(int fd, conn_io *conn);

};  // namespace quiche

#endif  // _QUICHECOMMON_H_INCLUDED_
