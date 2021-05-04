#include <blink/quicheClient.h>
#include <fcntl.h>
#include <inttypes.h>
#include <network/QSocket.h>
#include <rfb/LogWriter.h>
#include <unistd.h>

quiche_config* quiche::quiche_configure_client() {
  quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
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
  vlog.info("successfully configure quiche\n");

  if (getenv("SSLKEYLOGFILE")) {
    quiche_config_log_keys(config);
  }
}

