#include "mgw-basic.h"
#include "util/base.h"
#include "util/mgw-data.h"

#include "formats/ff-demuxing.h"

#include <stdio.h>


/** 
 * 1.set system config
 * 2.create stream 
 * 3.create source
 * 4.create output
 * 5.stop output
 * 6.stop source
 * 7.destroy stream
 * 8.system shutdown
 */

static struct mgw_stream *stream = NULL;
static void *demux = NULL;

void proc_packet(void *data, struct encoder_packet *packet)
{
	app_stream_send_private_packet(data, packet);
}

int main(int argc, char *argv[])
{
    mgw_data_t *source_settings = NULL;
    mgw_data_t *output_settings = NULL;
    blog(LOG_DEBUG, "------------  mgw test start ------------");
    if (mgw_app_startup("/home/young/workDir/mgw/install/mgw-config.json"))
        blog(LOG_DEBUG, "mgw startup success");
    else
        blog(LOG_DEBUG, "mgw startup failed");

    stream = app_stream_create("mgw_server_stream1");
    if (!stream)
        goto error;

    source_settings = mgw_data_create_from_json_file("/home/young/workDir/mgw/install/source-config.json");
    app_stream_add_private_source(stream, source_settings);

    /** start demux and send packet to source */
    demux = ff_demux_create("/home/young/workDir/mgw/install/test_mgw.mp4", false);
    ff_demux_start(demux, proc_packet, stream);

    output_settings = mgw_data_create_from_json_file("/home/young/workDir/mgw/install/output-config-template.json");
    app_stream_add_ouptut(stream, output_settings);

    output_settings = mgw_data_create_from_json_file("/home/young/workDir/mgw/install/output-config-template.json");
    mgw_data_set_int(output_settings, "channel", 2);
    mgw_data_set_string(output_settings, "id", "output2");
    mgw_data_set_string(output_settings, "key", "stream2");
    app_stream_add_ouptut(stream, output_settings);

	while('q' != getchar());

error:
	ff_demux_stop(demux);
	ff_demux_destroy(demux);
	mgw_app_exit();
	app_stream_destroy(stream);
	blog(LOG_DEBUG, "------------  mgw test end ------------");
    return 0;
}