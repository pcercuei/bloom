#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../deps/libretro-common/include/libretro.h"

static RETRO_CALLCONV void logcb(enum retro_log_level level,
      const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static RETRO_CALLCONV bool envcb(unsigned cmd, void *data)
{
	struct retro_log_callback *logging;
	struct retro_variable *var;
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		logging = data;
		logging->log = logcb;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
		var = data;
		if (!strcmp(var->key, "pcsx_rearmed_spu_thread")) {
			var->value = "enabled";
			return true;
		}
		break;
	}
	printf("%s %d %p\n", __func__, cmd, data);
	return false;
}

static RETRO_CALLCONV void pollcb(void) {}

int main(int argc, char *argv[])
{
	struct retro_system_info system_info;
	struct retro_system_av_info system_av_info;
	struct retro_game_info game_info = { "/dev/null", };

	if (argc != 999) {
		fprintf(stderr, "you're not supposed to run this file,"
			" it's only for link testing\n");
		//return 1;
	}

	retro_set_environment(envcb);
	retro_init();
	retro_set_video_refresh(NULL);
	retro_set_audio_sample(NULL);
	retro_set_audio_sample_batch(NULL);
	retro_set_input_poll(pollcb);
	retro_set_input_state(NULL);
	retro_api_version();
	retro_get_system_info(&system_info);
	retro_get_system_av_info(&system_av_info);
	retro_set_controller_port_device(0, 0);
	retro_load_game(&game_info);
	retro_reset();
	retro_run();
	retro_serialize_size();
	retro_serialize(NULL, 0);
	retro_unserialize(NULL, 0);
	retro_cheat_reset();
	retro_cheat_set(0, false, NULL);
	retro_load_game_special(0, NULL, 0);
	retro_unload_game();
	retro_get_region();
	retro_get_memory_data(0);
	retro_get_memory_size(0);
	retro_deinit();

	return 0;
}
