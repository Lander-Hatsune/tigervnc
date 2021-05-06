#ifndef _QUICHECLIENT_H_INCLUDED_
#define _QUICHECLIENT_H_INCLUDED_

#include <blink/quicheCommon.h>

namespace quiche {

quiche_config *quiche_configure_client();

conn_io *create_conn_client(const char *vncServerName, quiche_config *config);

}  // namespace quiche

#endif  // _QUICHECLIENT_H_INCLUDED_
