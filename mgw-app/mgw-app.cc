/**< C++ header files */
#include "message.h"

/**< C header files */
extern "C" {
#include "mgw-basic.h"
#include "util/base.h"
#include "util/tlog.h"
#include "util/mgw-data.h"
#include "formats/ff-demuxing.h"

#include <stdio.h>
#include <unistd.h>
}

/** 
 * 0.register to api and message server 
 * 1.set system config
 * 2.create stream 
 * 3.create source
 * 4.create output
 * 5.stop output
 * 6.stop source
 * 7.destroy stream
 * 8.system shutdown
 */

#define MGW_LOG_FILENAME	"mgw.log"
#define MGW_LOG_FILEPATH	"./"
#define MGW_LOG_FILELEN		1024*1024*2
#define MGW_LOG_FILECNT		10
#define MGW_LOG_BLOCKSIZE	1
#define MGW_LOG_BUFFERSIZE	0
#define MGW_LOG_MULTIWRITE	0

static void *stream = NULL;
static void *demux = NULL;

void proc_packet(void *data, struct encoder_packet *packet)
{
	mgw_stream_send_private_packet(data, packet);
}

static int mgw_stream_cb(int cmd, void *param, size_t param_size)
{
	return 0;
}

int main(int argc, char *argv[])
{
    mgw_data_t *source_settings = NULL;
    mgw_data_t *output_settings = NULL;
    blog(MGW_LOG_DEBUG, "------------  mgw test start ------------");

	tlog_init(MGW_LOG_FILENAME, MGW_LOG_FILELEN, MGW_LOG_FILECNT,
				MGW_LOG_BLOCKSIZE, MGW_LOG_BUFFERSIZE, MGW_LOG_MULTIWRITE);

	// Message &msg = Message::GetInstance();

	// if (msg_status::MSG_STATUS_SUCCESS !=
	// 	msg.Register("/home/young/workDir/mgw/install/bin/server-config-test.json")) {
	// 	blog(MGW_LOG_ERROR, "Register api and message server failed!");
	// 	goto finished;
	// }

    if (mgw_app_startup("mgw-config.json"))
        blog(MGW_LOG_DEBUG, "mgw startup success");
    else
        blog(MGW_LOG_DEBUG, "mgw startup failed");

    stream = mgw_stream_create("mgw_server_stream1", mgw_stream_cb);
    if (!stream)
        goto error;

    source_settings = mgw_data_create_from_json_file("source-config.json");
    mgw_stream_add_private_source(stream, source_settings);
    mgw_data_release(source_settings);

    /** start demux and send packet to source */
	demux = ff_demux_create("/home/young/workDir/mgw/install/bin/ppp.mp4", false);
	ff_demux_start(demux, proc_packet, stream);

	// demux = ff_demux_create("rtmp://192.168.0.16/live/stream0", false);

	char code;
	while('q' != (code = getchar()))
    {
		switch(code) {
            case '1': {
                output_settings = mgw_data_create_from_json_file("output-config-template.json");
                mgw_stream_add_ouptut(stream, output_settings);
                mgw_data_release(output_settings);
                break;
            }
            case '2': mgw_stream_release_output(stream, "output1"); break;
			case '3': {
				output_settings = mgw_data_create_from_json_file("output-config-template.json");
				mgw_data_set_int(output_settings, "channel", 2);
				mgw_data_set_string(output_settings, "id", "output2");
				mgw_data_set_string(output_settings, "path", "srt://192.168.0.24:4301?streamid=#!::h=live/livestream,m=publish");
				mgw_data_set_string(output_settings, "protocol", "srt");
                mgw_stream_add_ouptut(stream, output_settings);
                mgw_data_release(output_settings);
				break;
			}
			case '4':mgw_stream_release_output(stream, "output2"); break;
			default: break;
		}
		usleep(50 * 1000);
    }

error:
	ff_demux_stop(demux);
	ff_demux_destroy(demux);
	mgw_stream_destroy(stream);
	mgw_app_exit();
finished:
	blog(MGW_LOG_DEBUG, "------------  mgw test end ------------");
	return 0;
}