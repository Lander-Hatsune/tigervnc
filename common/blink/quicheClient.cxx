#include <blink/quicheClient.h>
#include <fcntl.h>
#include <inttypes.h>
#include <network/QSocket.h>
#include <rfb/LogWriter.h>
#include <unistd.h>

using namespace quiche;
static rfb::LogWriter vlog("quicheClient");

quiche_config *quiche::quiche_configure_client() {
  quiche_config *config = quiche_config_new(0xbabababa);
  if (config == NULL) {
    // fprintf(stderr, "failed to create config\n");
    // return -1;
    throw QuicheException("failed to create quiche config", 0);
  }

  quiche_config_set_application_protos(
      config, (uint8_t *)"\x05hq-28\x05hq-27\x08http/0.9", 21);

  quiche_config_set_max_idle_timeout(config, 5000);
  quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);
  quiche_config_set_initial_max_data(config, 10000000);
  quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);
  quiche_config_set_initial_max_stream_data_uni(config, 1000000);
  quiche_config_set_initial_max_streams_bidi(config, 100);
  quiche_config_set_initial_max_streams_uni(config, 100);
  quiche_config_set_disable_active_migration(config, true);
  vlog.info("successfully configure quiche");

  if (getenv("SSLKEYLOGFILE")) {
    quiche_config_log_keys(config);
  }
  return config;
}

conn_io *quiche::create_conn_client(const char *vncServerName,
                                    quiche_config *config) {
  conn_io *conn = new conn_io;
  if (conn == NULL) {
    throw QuicheException("failed to allocate connection IO", -1);
    return NULL;
  }

  uint8_t scid[LOCAL_CONN_ID_LEN];
  int rng = open("/dev/urandom", O_RDONLY);
  if (rng < 0) {
    throw QuicheException("failed to open /dev/urandom", -1);
    return NULL;
  }

  ssize_t rand_len = read(rng, &scid, sizeof(scid));
  if (rand_len < 0) {
    throw QuicheException("failed to create connection ID", -1);
    return NULL;
  }

  quiche_conn *q_conn = quiche_connect(vncServerName, (const uint8_t *)scid,
                                       sizeof(scid), config);
  if (q_conn == NULL) {
    throw QuicheException("failed to create connection", -1);
    return NULL;
  }

  conn->q_conn = q_conn;

  return conn;
}