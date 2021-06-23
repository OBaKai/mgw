#include <iostream>
#include "util/mgw-data.h"

void json_data_get_test(void)
{
    /* get and printf base type value */
    mgw_data_t *setting = mgw_data_create_from_json_file("mgw-config.json");

    std::cout << std::endl;

    const char *name = mgw_data_get_string(setting, "name");
    if (name)
        std::cout << "name:" << name << std::endl;

    std::cout << std::endl;

    /* devices array */
    mgw_data_array_t *devices = mgw_data_get_array(setting, "devices");
    if (devices) {
        size_t dev_count = mgw_data_array_count(devices);
        std::cout << "dev_count:" << dev_count << std::endl;

        std::cout << std::endl;

        for (size_t i = 0; i < dev_count; i++) {
            mgw_data_t *dev = mgw_data_array_item(devices, i);
            const char *type = mgw_data_get_string(dev, "type");
            if (type)
                std::cout << "device type:" << type << std::endl;
            const char *sn = mgw_data_get_string(dev, "SN");
            if (sn)
                std::cout << "device SN:" << sn << std::endl;
            
            std::cout << std::endl;

            /* input stream array */
            mgw_data_array_t *input_stream = mgw_data_get_array(dev, "input_stream");
            if (input_stream) {
                size_t input_count = mgw_data_array_count(input_stream);
                std::cout << "input_count:" << input_count << std::endl;
            }

            std::cout << std::endl;

            /* output stream array */
            mgw_data_array_t *output_stream = mgw_data_get_array(dev, "output_stream");
            if (output_stream) {

                size_t output_count = mgw_data_array_count(output_stream);
                std::cout << "output_count:" << output_count << std::endl;

                std::cout << std::endl;

                for (size_t i = 0; i < output_count; i++) {
                    mgw_data_t *stream = mgw_data_array_item(output_stream, i);

                    int channel = mgw_data_get_int(stream, "channel");
                    std::cout << "channel:" << channel << std::endl;

                    bool active = mgw_data_get_bool(stream, "active");
                    std::cout << "active:" << std::boolalpha << active << std::endl;

                    double kbps = mgw_data_get_double(stream, "kbps");
                    std::cout << "kbps:" << kbps << std::endl;

                    const char *protocol = mgw_data_get_string(stream, "protocol");
                    std::cout << "protocol:" << protocol << std::endl;

                    const char *url = mgw_data_get_string(stream, "url");
                    std::cout << "url:" << url << std::endl;

                    const char *key = mgw_data_get_string(stream, "key");
                    std::cout << "key:" << key << std::endl;

                    const char *username = mgw_data_get_string(stream, "username");
                    std::cout << "username:" << username << std::endl;

                    const char *password = mgw_data_get_string(stream, "password");
                    std::cout << "password:" << password << std::endl;

                    const char *dest_ip = mgw_data_get_string(stream, "dest_ip");
                    std::cout << "dest_ip:" << dest_ip << std::endl;

                    std::cout << std::endl;
                }
            }
            std::cout << std::endl;
        }
    }

}

void json_data_set_test(void)
{
    const char *file = "mgw-data.json";
    mgw_data_t *data = mgw_data_create();

    /* set a string */
    mgw_data_set_string(data, "config", "data");

    /* set a int value */
    mgw_data_set_int(data, "test_int", 10);

    /* set a bool value */
    mgw_data_set_bool(data, "enable", false);

    /* set a double value */
    mgw_data_set_double(data, "double_test", 16985.349846156489);

    /* set a object */
    mgw_data_t *obj_test = mgw_data_create();
    mgw_data_set_obj(data, "obj_test", obj_test);

    /* set a array */
    mgw_data_array_t *array_test = mgw_data_array_create();
    mgw_data_set_array(data, "array_test", array_test);

    mgw_data_save_json(data, file);
}

int main(int argc, char *argv[])
{
    std::cout << "------------------------------------ json data get test -----------------------" << std::endl;
    json_data_get_test();

    std::cout << "------------------------------------ json data set test -----------------------" << std::endl;
    json_data_set_test();

    return 0;
}
