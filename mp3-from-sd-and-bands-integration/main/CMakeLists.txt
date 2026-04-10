idf_component_register(
	SRCS
		"main.c"

		"audio_control.c"
		"console.c"
		"console_settings.c"
		"espnow_transport.c"
		"fan_control.c"
		"pid_controller.c"
		"sd_card.c"
		"visualizer.c"
	INCLUDE_DIRS
		"."
)