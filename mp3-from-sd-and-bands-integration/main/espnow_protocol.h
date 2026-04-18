#pragma once

#include <stdint.h>
#include <stdbool.h>

#define ESPNOW_PROTOCOL_MAGIC 0x5AA55A

// We can safely fit 15 song names (max 64 chars each) in a single ESP-NOW packet
#define MAX_SONGS_PER_PACKET 15
#define MAX_SONG_NAME_LEN 64

// Message Categories
typedef enum {
	MSG_TYPE_COMMAND = 0,
	MSG_TYPE_REQUEST,
	MSG_TYPE_TELEMETRY
} msg_type_t;

// Commands (GUI -> Audio)
typedef enum {
	CMD_PLAY_INDEX = 0,
	CMD_PLAY_PAUSE,
	CMD_NEXT,
	CMD_PREVIOUS,
	CMD_SET_VOLUME,
} command_id_t;

// Telemetry Types (Audio -> GUI)
typedef enum {
	TEL_PLAYBACK_STATUS = 0, // Sending time/progress/state
	TEL_PLAYLIST_CHUNK       // Sending a chunk of song names
} telemetry_id_t;

typedef enum {
	TEL_STATE_STOPPED = 0,
	TEL_STATE_PLAYING,
	TEL_STATE_PAUSED
} playback_state_t;

// Payload for Commands
typedef struct {
	command_id_t id;
	int32_t value;
} command_payload_t;

// Payload for Status Telemetry
typedef struct {
	playback_state_t state;
	int32_t current_song_index;
	int32_t elapsed_seconds;
	int32_t duration_seconds;
} tel_status_t;

// NEW: Payload for Playlist Telemetry
typedef struct {
	int32_t start_index;      // The index of the first song in this packet (e.g., 0, 15, 30)
	int32_t total_songs;      // Total songs found on the SD card
	int32_t count;            // How many valid song strings are in this specific packet
	char songs[MAX_SONGS_PER_PACKET][MAX_SONG_NAME_LEN];
} tel_playlist_t;

// Master Telemetry Payload
typedef struct {
	telemetry_id_t id;
	union {
		tel_status_t status;
		tel_playlist_t playlist;
	} data;
} telemetry_payload_t;

// The Master Packet
typedef struct {
	uint32_t magic;
	msg_type_t type;

	union {
		command_payload_t command;
		telemetry_payload_t telemetry;
	} payload; // Renamed to 'payload' for clarity
} espnow_packet_t;