#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_power.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#include <expansion/expansion.h>

#include <gui/gui.h>
#include <gui/elements.h>
#include <gui/modules/text_input.h>
#include <gui/modules/submenu.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>

#include "flipperha_icons.h"

#include <notification/notification_messages.h>

#include <storage/storage.h>

#include <stdlib.h>
#include <string.h>

#define HA_REMOTE_LOG_TAG "HA_REMOTE"

#define HA_REMOTE_VIEW_ROOT 0
#define HA_REMOTE_VIEW_CONTROLLER 1
#define HA_REMOTE_VIEW_THERMOSTAT 2
#define HA_REMOTE_VIEW_SETTINGS 3
#define HA_REMOTE_VIEW_ABOUT 4
#define HA_REMOTE_VIEW_DIMMER 5
#define HA_REMOTE_VIEW_CONTROLLER_INFO 6
#define HA_REMOTE_VIEW_ADD 7
#define HA_REMOTE_VIEW_ADD_TEXT 8
#define HA_REMOTE_BEACON_TIMER_MS 1500
#define HA_REMOTE_STATUS_TIMER_MS 3000
#define HA_REMOTE_COMMAND_CANCEL_WINDOW_MS 750
#define HA_REMOTE_LOADING_STEP_MS 250
// Marquee (chyron) for the long entity_id on the Add -> Info screen.
#define HA_REMOTE_MARQUEE_TICK_MS 220
#define HA_REMOTE_MARQUEE_HOLD_TICKS 7
#define HA_REMOTE_MARQUEE_VISIBLE_CHARS 20
#define HA_REMOTE_MARQUEE_HOLD_START 0
#define HA_REMOTE_MARQUEE_SCROLL 1
#define HA_REMOTE_MARQUEE_HOLD_END 2
#define HA_REMOTE_MARQUEE_SCROLL_BACK 3
#define HA_REMOTE_COMMAND_QUEUE_CAPACITY 15
#define HA_REMOTE_COMMAND_RESULT_QUEUE_CAPACITY (HA_REMOTE_COMMAND_QUEUE_CAPACITY + 1)
#define HA_REMOTE_COMMAND_WORKER_STACK_SIZE 6144
#define HA_REMOTE_CONTROLLER_VISIBLE_ROWS 4
#define HA_REMOTE_CONTROLLER_TOOL_COUNT 3
#define HA_REMOTE_CONTROLLER_TOOL_INFO 0
#define HA_REMOTE_CONTROLLER_TOOL_REORDER 1
#define HA_REMOTE_CONTROLLER_TOOL_PLUS 2
#define HA_REMOTE_CONTROLLER_RAIL_X 3
#define HA_REMOTE_CONTROLLER_RAIL_Y 15
#define HA_REMOTE_CONTROLLER_RAIL_BUTTON_SIZE 13
#define HA_REMOTE_CONTROLLER_RAIL_BUTTON_GAP 4
#define HA_REMOTE_CONTROLLER_LIST_X 21
#define HA_REMOTE_CONTROLLER_HANDLE_X 20
#define HA_REMOTE_CONTROLLER_ROW_RIGHT 124
#define HA_REMOTE_CONTROLLER_LABEL_X 24
#define HA_REMOTE_CONTROLLER_STATE_X 119
#define HA_REMOTE_CONTROLLER_SCROLLBAR_X 125
#define HA_REMOTE_CONTROLLER_DELETE_BOX_X 113
#define HA_REMOTE_CONTROLLER_DELETE_BOX_SIZE 10
#define HA_REMOTE_DIMMER_TAP_STEP 1
#define HA_REMOTE_DIMMER_REPEAT_MED_AFTER 2
#define HA_REMOTE_DIMMER_REPEAT_FAST_AFTER 5
#define HA_REMOTE_DIMMER_REPEAT_TURBO_AFTER 12
#define HA_REMOTE_ORDER_FILE APP_DATA_PATH("controller_order.bin")
#define HA_REMOTE_ENTRY_FILE APP_DATA_PATH("controller_entries.bin")
#define HA_REMOTE_ORDER_MAGIC 0x314F5248UL
#define HA_REMOTE_ORDER_VERSION 1
#define HA_REMOTE_ENTRY_MAGIC 0x324F5248UL
#define HA_REMOTE_ENTRY_VERSION 2
#define HA_REMOTE_SETTINGS_FILE APP_DATA_PATH("settings.bin")
#define HA_REMOTE_SETTINGS_MAGIC 0x54455348UL
#define HA_REMOTE_SETTINGS_VERSION 1
#define HA_REMOTE_CUSTOM_ENTRY_MAX 12
#define HA_REMOTE_CONTROLLER_ENTRY_MAX (HA_REMOTE_ACTION_COUNT + HA_REMOTE_CUSTOM_ENTRY_MAX)
#define HA_REMOTE_ENTITY_ID_MAX 72
#define HA_REMOTE_ENTITY_TYPE_MAX 16
#define HA_REMOTE_ADD_VISIBLE_ROWS 4
#define HA_REMOTE_ADD_CATALOG_PAGE_SIZE 4
#define HA_REMOTE_ADD_ITEM_MAX 4

#define HA_REMOTE_REFRESH_INDEX 0
#define HA_REMOTE_ACTION_MENU_BASE 1

#define HA_REMOTE_ROOT_CONTROLLER_INDEX 0
#define HA_REMOTE_ROOT_THERMOSTAT_INDEX 1
#define HA_REMOTE_ROOT_SETTINGS_INDEX 2
#define HA_REMOTE_ROOT_ABOUT_INDEX 3

#define HA_REMOTE_SETTINGS_VIBRATION_INDEX 0
#define HA_REMOTE_SETTINGS_LOG_INDEX 1
#define HA_REMOTE_SETTINGS_BLE_HINT_INDEX 2

#define HA_REMOTE_ABOUT_VERSION_INDEX 0
#define HA_REMOTE_ABOUT_WIFI_INDEX 1
#define HA_REMOTE_ABOUT_BLE_INDEX 2
#define HA_REMOTE_ABOUT_STATE_INDEX 3
#define HA_REMOTE_ABOUT_THERMO_INDEX 4

#define HA_REMOTE_DEFAULT_BRIDGE_BASE_URL "https://your-bridge.example.com/v1"
#define HA_REMOTE_DEFAULT_BRIDGE_KEY "flipper"
#define HA_REMOTE_BRIDGE_CONFIG_FILE APP_DATA_PATH("bridge.cfg")
#define HA_REMOTE_THERMOSTAT_CONFIG_FILE APP_DATA_PATH("thermostat.cfg")
#define HA_REMOTE_BRIDGE_URL_MAX 160
#define HA_REMOTE_BRIDGE_KEY_MAX 96
#define HA_REMOTE_COMMAND_PATH_MAX 416

#if __has_include("ha_remote_local.h")
#include "ha_remote_local.h"
#endif

#ifndef FLIPPERHA_BRIDGE_BASE_URL
#ifdef HA_REMOTE_BRIDGE_BASE_URL
#define FLIPPERHA_BRIDGE_BASE_URL HA_REMOTE_BRIDGE_BASE_URL
#else
#define FLIPPERHA_BRIDGE_BASE_URL HA_REMOTE_DEFAULT_BRIDGE_BASE_URL
#endif
#endif

#ifndef FLIPPERHA_BRIDGE_KEY
#ifdef HA_REMOTE_BRIDGE_KEY
#define FLIPPERHA_BRIDGE_KEY HA_REMOTE_BRIDGE_KEY
#else
#define FLIPPERHA_BRIDGE_KEY HA_REMOTE_DEFAULT_BRIDGE_KEY
#endif
#endif
#define HA_REMOTE_UART_BAUD 115200
#define HA_REMOTE_UART_STREAM_SIZE 1024
#define HA_REMOTE_UART_READY_DELAY_MS 100
#define HA_REMOTE_UART_PING_RETRIES 3
#define HA_REMOTE_UART_PING_TIMEOUT_MS 1800
#define HA_REMOTE_UART_PING_RETRY_DELAY_MS 250
#define HA_REMOTE_HTTP_RESPONSE_MAX 1024
#define HA_REMOTE_HTTP_BODY_MAX 768
#define HA_REMOTE_HTTP_COMMAND_MAX 544
// Cap the batched-state request path so the full [GET] command stays under the
// FlipperHTTP UART command ceiling (~512B). Rows beyond this fall back to unknown.
#define HA_REMOTE_SYNC_PATH_BUDGET 380
#define HA_REMOTE_LABEL_MAX 40
#define HA_REMOTE_STATE_MAX 12
#define HA_REMOTE_THERMO_VALUE_MAX 20
#define HA_REMOTE_HEADER_MAX 32
#define HA_REMOTE_ROOT_ITEM_COUNT 4
#define HA_REMOTE_INFO_LINE_COUNT 5
#define HA_REMOTE_INFO_LINE_MAX 40

#define BTHOME_UUID_LSB 0xD2
#define BTHOME_UUID_MSB 0xFC

#define BTHOME_AD_TYPE_FLAGS 0x01
#define BTHOME_AD_TYPE_COMPLETE_LOCAL_NAME 0x09
#define BTHOME_AD_TYPE_SERVICE_DATA 0x16

#define BTHOME_OBJ_PACKET_ID 0x00
#define BTHOME_OBJ_BATTERY 0x01
#define BTHOME_OBJ_COMMAND 0x3B

#define BTHOME_DEVICE_INFO_TRIGGER_V2 0x44
#define BTHOME_COMMAND_ARG_LEN_1 0x01
#define BTHOME_COMMAND_STEP_UP 0x03

typedef enum {
    HaRemoteActionKindToggle,
    HaRemoteActionKindRun,
    HaRemoteActionKindDimmer,
} HaRemoteActionKind;

typedef struct {
    const char* label;
    uint8_t action_id;
    HaRemoteActionKind kind;
} HaRemoteAction;

#ifndef HA_REMOTE_ENABLE_EXAMPLE_ACTIONS
#define HA_REMOTE_ENABLE_EXAMPLE_ACTIONS 0
#endif

#if HA_REMOTE_ENABLE_EXAMPLE_ACTIONS
static const HaRemoteAction ha_remote_actions[] = {
    {"Example Switch", 0x01, HaRemoteActionKindToggle},
    {"Example Dimmer", 0x02, HaRemoteActionKindDimmer},
    {"Example Scene", 0x03, HaRemoteActionKindRun},
};
#define HA_REMOTE_ACTION_COUNT (sizeof(ha_remote_actions) / sizeof(ha_remote_actions[0]))
#else
static const HaRemoteAction* const ha_remote_actions = NULL;
#define HA_REMOTE_ACTION_COUNT 0
#endif
#define HA_REMOTE_ACTION_STORAGE_COUNT (HA_REMOTE_ACTION_COUNT > 0 ? HA_REMOTE_ACTION_COUNT : 1)

static bool ha_remote_entry_is_builtin(uint8_t entry_id) {
#if HA_REMOTE_ACTION_COUNT > 0
    return entry_id < HA_REMOTE_ACTION_COUNT;
#else
    UNUSED(entry_id);
    return false;
#endif
}

typedef enum {
    HaRemoteCustomKindToggle,
    HaRemoteCustomKindDimmer,
    HaRemoteCustomKindRoutine,
} HaRemoteCustomKind;

typedef struct {
    uint8_t used;
    uint8_t kind;
    char label[HA_REMOTE_LABEL_MAX];
    char entity_id[HA_REMOTE_ENTITY_ID_MAX];
    char friendly_name[HA_REMOTE_LABEL_MAX];
    char type_label[HA_REMOTE_ENTITY_TYPE_MAX];
} HaRemoteCustomEntry;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t visible_count;
    uint8_t total_count;
    uint8_t custom_count;
    uint8_t order[HA_REMOTE_CONTROLLER_ENTRY_MAX];
    HaRemoteCustomEntry custom[HA_REMOTE_CUSTOM_ENTRY_MAX];
} HaRemoteControllerEntryFile;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t log_mode;
    uint8_t vibration_enabled;
    uint8_t reserved;
} HaRemoteSettingsFile;

typedef enum {
    HaRemoteLogModeLight,
    HaRemoteLogModeError,
    HaRemoteLogModeDebug,
    HaRemoteLogModeOff,
    HaRemoteLogModeCount,
} HaRemoteLogMode;

static const char* ha_remote_log_mode_labels[] = {
    "Light",
    "Error",
    "Debug",
    "Off",
};

typedef enum {
    HaRemoteThermoFocusSetpoint,
    HaRemoteThermoFocusMode,
    HaRemoteThermoFocusFan,
    HaRemoteThermoFocusSave,
} HaRemoteThermoFocus;

typedef struct {
    const char* label;
    const char* value;
    const char* path_value;
} HaRemoteThermoOption;

static const HaRemoteThermoOption ha_remote_thermo_modes[] = {
    {"Cool", "cool", "cool"},
    {"Heat", "heat", "heat"},
    {"H/C", "heat_cool", "heat_cool"},
    {"Off", "off", "off"},
};

static const HaRemoteThermoOption ha_remote_thermo_fans[] = {
    {"Circ", "Circulation", "Circulation"},
    {"Auto", "Auto low", "Auto%20low"},
    {"Low", "Low", "Low"},
};

#define HA_REMOTE_THERMO_MODE_COUNT \
    (sizeof(ha_remote_thermo_modes) / sizeof(ha_remote_thermo_modes[0]))
#define HA_REMOTE_THERMO_FAN_COUNT (sizeof(ha_remote_thermo_fans) / sizeof(ha_remote_thermo_fans[0]))

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint8_t order[HA_REMOTE_ACTION_STORAGE_COUNT];
} HaRemoteControllerOrderFile;

typedef struct {
    char status[HA_REMOTE_HEADER_MAX];
    char actual[HA_REMOTE_THERMO_VALUE_MAX];
    char humidity[HA_REMOTE_THERMO_VALUE_MAX];
    char current_target[HA_REMOTE_THERMO_VALUE_MAX];
    char action[HA_REMOTE_THERMO_VALUE_MAX];
    int16_t actual_target;
    int16_t draft_target;
    uint8_t actual_mode;
    uint8_t draft_mode;
    uint8_t edit_mode_start;
    uint8_t actual_fan;
    uint8_t draft_fan;
    uint8_t edit_fan_start;
    uint8_t mode_mask;
    uint8_t fan_mask;
    HaRemoteThermoFocus focus;
    bool configured;
    bool known;
    bool fan_supported;
    bool editing;
    bool dirty;
} HaRemoteThermostatModel;

typedef struct {
    char header[HA_REMOTE_HEADER_MAX];
    uint8_t selected;
} HaRemoteRootModel;

typedef struct {
    char title[HA_REMOTE_HEADER_MAX];
    char queue[8];
    char delete_label[HA_REMOTE_LABEL_MAX];
    char labels[HA_REMOTE_CONTROLLER_ENTRY_MAX][HA_REMOTE_LABEL_MAX];
    char states[HA_REMOTE_CONTROLLER_ENTRY_MAX][HA_REMOTE_STATE_MAX];
    bool state_known[HA_REMOTE_CONTROLLER_ENTRY_MAX];
    uint8_t action_order[HA_REMOTE_CONTROLLER_ENTRY_MAX];
    uint8_t action_order_count;
    uint8_t item_count;
    uint8_t selected;
    uint8_t scroll;
    uint8_t tool_selected;
    bool queue_visible;
    bool syncing;
    bool tool_focus;
    bool reorder_mode;
    bool reorder_grabbed;
    bool reorder_delete_focus;
    bool reorder_delete_confirm;
} HaRemoteControllerModel;

typedef struct {
    char title[HA_REMOTE_LABEL_MAX];
    uint8_t entry_id;
    uint8_t value;
    uint8_t original;
    uint8_t repeat_count;
    InputKey repeat_key;
    bool known;
} HaRemoteDimmerModel;

typedef struct {
    char title[HA_REMOTE_HEADER_MAX];
    char lines[HA_REMOTE_INFO_LINE_COUNT][HA_REMOTE_INFO_LINE_MAX];
} HaRemoteControllerInfoModel;

typedef enum {
    HaRemoteAddCategorySwitches,
    HaRemoteAddCategoryLights,
    HaRemoteAddCategoryRoutines,
    HaRemoteAddCategoryClimate,
    HaRemoteAddCategoryCount,
} HaRemoteAddCategory;

#define HA_REMOTE_ADD_CONTROLLER_CATEGORY_COUNT HaRemoteAddCategoryClimate

typedef enum {
    HaRemoteAddModeCategories,
    HaRemoteAddModeBrowser,
    HaRemoteAddModeInfo,
    HaRemoteAddModeConfirm,
} HaRemoteAddMode;

typedef enum {
    HaRemoteAddOriginController,
    HaRemoteAddOriginThermostat,
} HaRemoteAddOrigin;

typedef enum {
    HaRemoteAddButtonLabel,
    HaRemoteAddButtonInfo,
    HaRemoteAddButtonAdd,
} HaRemoteAddButton;

typedef struct {
    char entity_id[HA_REMOTE_ENTITY_ID_MAX];
    char label[HA_REMOTE_LABEL_MAX];
    char type_label[HA_REMOTE_ENTITY_TYPE_MAX];
    char capability[10];
} HaRemoteCatalogItem;

typedef struct {
    char title[HA_REMOTE_HEADER_MAX];
    char status[HA_REMOTE_HEADER_MAX];
    char draft_label[HA_REMOTE_LABEL_MAX];
    HaRemoteCatalogItem items[HA_REMOTE_ADD_ITEM_MAX];
    uint8_t item_count;
    uint8_t selected;
    uint8_t button;
    uint8_t category;
    uint8_t mode;
    uint8_t total_count;
    uint8_t offset;
    uint8_t confirm_focus;
    uint16_t marquee_offset;
    bool loading;
} HaRemoteAddModel;

typedef enum {
    HaRemoteCustomEventClearStatus = 1,
    HaRemoteCustomEventDispatchPendingCommand,
    HaRemoteCustomEventControllerSyncComplete,
    HaRemoteCustomEventCommandComplete,
    HaRemoteCustomEventCommandStatusChanged,
    HaRemoteCustomEventAddLoadComplete,
    HaRemoteCustomEventMarqueeTick,
} HaRemoteCustomEvent;

typedef enum {
    HaRemotePendingCommandNone,
    HaRemotePendingCommandAction,
    HaRemotePendingCommandDimmer,
    HaRemotePendingCommandThermostat,
    HaRemotePendingCommandStop,
} HaRemotePendingCommandType;

typedef struct {
    HaRemotePendingCommandType type;
    uint8_t action_index;
    char path[HA_REMOTE_COMMAND_PATH_MAX];
} HaRemoteQueuedCommand;

typedef struct {
    HaRemotePendingCommandType type;
    uint8_t action_index;
    bool ok;
    char path[HA_REMOTE_COMMAND_PATH_MAX];
    char body[HA_REMOTE_HTTP_BODY_MAX];
} HaRemoteCommandResult;

typedef struct {
    ViewDispatcher* view_dispatcher;
    View* root_view;
    View* controller_view;
    View* thermostat_view;
    View* dimmer_view;
    View* controller_info_view;
    Submenu* settings_menu;
    Submenu* about_menu;
    NotificationApp* notifications;

    FuriThread* controller_sync_thread;
    FuriThread* command_worker_thread;
    FuriMessageQueue* command_queue;
    FuriMessageQueue* command_result_queue;
    FuriMutex* bridge_mutex;
    FuriTimer* beacon_timer;
    FuriTimer* status_timer;
    FuriTimer* command_timer;
    FuriTimer* marquee_timer;
    uint16_t marquee_offset;
    uint16_t marquee_max_offset;
    uint16_t marquee_tick;
    uint8_t marquee_phase;
    uint8_t packet_id;

    uint8_t beacon_payload[EXTRA_BEACON_MAX_DATA_SIZE];
    size_t beacon_payload_len;
    size_t packet_id_offset;
    size_t battery_offset;
    size_t command_arg_offset;

    FuriStreamBuffer* uart_stream;
    FuriHalSerialHandle* uart_handle;
    Expansion* expansion;
    bool expansion_opened;

    char action_states[HA_REMOTE_ACTION_STORAGE_COUNT][HA_REMOTE_STATE_MAX];
    bool action_state_known[HA_REMOTE_ACTION_STORAGE_COUNT];
    HaRemoteCustomEntry custom_entries[HA_REMOTE_CUSTOM_ENTRY_MAX];
    char custom_states[HA_REMOTE_CUSTOM_ENTRY_MAX][HA_REMOTE_STATE_MAX];
    bool custom_state_known[HA_REMOTE_CUSTOM_ENTRY_MAX];
    uint8_t custom_entry_count;
    char thermostat_temp[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_target[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_mode[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_action[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_fan[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_humidity[HA_REMOTE_THERMO_VALUE_MAX];
    char thermostat_entity_id[HA_REMOTE_ENTITY_ID_MAX];
    uint8_t thermostat_mode_mask;
    uint8_t thermostat_fan_mask;
    bool thermostat_configured;
    bool thermostat_state_known;
    char vibration_label[HA_REMOTE_LABEL_MAX];
    char log_mode_label[HA_REMOTE_LABEL_MAX];
    char http_response[HA_REMOTE_HTTP_RESPONSE_MAX];
    char http_body[HA_REMOTE_HTTP_BODY_MAX];
    char http_command[HA_REMOTE_HTTP_COMMAND_MAX];
    char controller_sync_body[HA_REMOTE_HTTP_BODY_MAX];
    char pending_command_path[HA_REMOTE_COMMAND_PATH_MAX];
    char bridge_base_url[HA_REMOTE_BRIDGE_URL_MAX];
    char bridge_key[HA_REMOTE_BRIDGE_KEY_MAX];
    char status_header[HA_REMOTE_HEADER_MAX];

    const char* loading_label;
    uint32_t loading_last_tick;
    uint8_t loading_step;
    uint8_t root_selected;
    uint8_t controller_selected;
    uint8_t controller_scroll;
    uint8_t controller_tool_selected;
    uint8_t action_order[HA_REMOTE_CONTROLLER_ENTRY_MAX];
    uint8_t action_order_count;
    uint8_t pending_action_index;
    uint8_t command_worker_action_index;
    HaRemotePendingCommandType pending_command_type;
    HaRemotePendingCommandType command_worker_type;
    bool loading_enabled;
    bool vibration_enabled;
    bool status_clear_enabled;
    bool command_worker_busy;
    bool command_worker_started;
    bool controller_sync_running;
    bool controller_sync_ok;
    bool controller_sync_notify;
    bool bridge_configured;
    bool bridge_config_from_file;
    bool controller_tool_focus;
    bool controller_reorder_mode;
    bool controller_reorder_grabbed;
    bool controller_reorder_delete_focus;
    bool controller_reorder_delete_confirm;
    bool rename_active;
    uint8_t rename_custom_index;
    View* add_view;
    TextInput* add_text_input;
    FuriThread* add_load_thread;
    char add_body[HA_REMOTE_HTTP_BODY_MAX];
    char add_text_buffer[HA_REMOTE_LABEL_MAX];
    HaRemoteCatalogItem add_items[HA_REMOTE_ADD_ITEM_MAX];
    uint8_t add_item_count;
    uint8_t add_total_count;
    uint8_t add_offset;
    uint8_t add_selected;
    uint8_t add_button;
    uint8_t add_category;
    uint8_t add_request_category;
    uint8_t add_mode;
    uint8_t add_origin;
    uint8_t add_request_offset;
    uint8_t add_confirm_focus;
    bool add_loading;
    bool add_load_ok;
    bool add_load_again;
    bool views_added;
    HaRemoteLogMode log_mode;
    uint32_t current_view;
} HaRemoteApp;

static void ha_remote_update_labels(HaRemoteApp* app);
static void ha_remote_controller_model_sync(HaRemoteApp* app, bool update);
static void ha_remote_controller_info_model_sync(HaRemoteApp* app, bool update);
static void ha_remote_add_model_sync(HaRemoteApp* app, bool update);
static bool ha_remote_custom_index_from_entry(
    HaRemoteApp* app,
    uint8_t entry_id,
    uint8_t* custom_index);
static bool ha_remote_parse_states(HaRemoteApp* app, const char* body);
static bool ha_remote_parse_thermostat(HaRemoteApp* app, const char* body);
static bool ha_remote_value_equals(const char* value, const char* expected);
static bool ha_remote_state_is_off(const char* value);
static bool ha_remote_refresh_thermostat(HaRemoteApp* app, bool notify);
static void ha_remote_sync_thermostat_model(HaRemoteApp* app, bool preserve_dirty);
static void ha_remote_thermostat_set_status(HaRemoteApp* app, const char* status);
static void ha_remote_uart_close(HaRemoteApp* app);
static bool ha_remote_dispatch_pending_command(HaRemoteApp* app);
static bool ha_remote_cancel_pending_command(HaRemoteApp* app, const char* header);
static void ha_remote_complete_command_results(HaRemoteApp* app);
static int32_t ha_remote_command_worker(void* context);
static void ha_remote_stop_command_worker(HaRemoteApp* app);
static bool ha_remote_start_controller_sync(HaRemoteApp* app, bool notify);
static void ha_remote_join_controller_sync(HaRemoteApp* app);
static void ha_remote_complete_controller_sync(HaRemoteApp* app);
static bool ha_remote_start_add_load(HaRemoteApp* app);
static void ha_remote_join_add_load(HaRemoteApp* app);
static void ha_remote_complete_add_load(HaRemoteApp* app);
static void ha_remote_add_text_callback(void* context);
static void ha_remote_rename_text_callback(void* context);
static void ha_remote_open_rename(HaRemoteApp* app, uint8_t entry_id);
static void ha_remote_root_callback(void* context, InputType input_type, uint32_t index);
static void ha_remote_controller_callback(void* context, InputType input_type, uint32_t index);
static void ha_remote_switch_to_view(HaRemoteApp* app, uint32_t view_id);
static bool ha_remote_uart_read_window(
    HaRemoteApp* app,
    char* out,
    size_t out_size,
    uint32_t timeout_ms,
    const char* stop_marker,
    bool loading);

static void ha_remote_beacon_timer_callback(void* context) {
    UNUSED(context);

    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }
}

static void ha_remote_status_timer_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    view_dispatcher_send_custom_event(app->view_dispatcher, HaRemoteCustomEventClearStatus);
}

static void ha_remote_command_timer_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    view_dispatcher_send_custom_event(app->view_dispatcher, HaRemoteCustomEventDispatchPendingCommand);
}

static void ha_remote_marquee_start(HaRemoteApp* app, const char* text) {
    app->marquee_offset = 0;
    app->marquee_phase = HA_REMOTE_MARQUEE_HOLD_START;
    app->marquee_tick = 0;
    size_t len = text ? strlen(text) : 0;
    app->marquee_max_offset = len > HA_REMOTE_MARQUEE_VISIBLE_CHARS ?
                                  (uint16_t)(len - HA_REMOTE_MARQUEE_VISIBLE_CHARS) :
                                  0;
    if(app->marquee_timer) {
        if(app->marquee_max_offset > 0) {
            furi_timer_start(app->marquee_timer, HA_REMOTE_MARQUEE_TICK_MS);
        } else {
            furi_timer_stop(app->marquee_timer);
        }
    }
}

static void ha_remote_marquee_timer_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    // Self-stop once we leave the Info screen (so every exit path is covered).
    if(app->current_view != HA_REMOTE_VIEW_ADD || app->add_mode != HaRemoteAddModeInfo ||
       app->marquee_max_offset == 0) {
        furi_timer_stop(app->marquee_timer);
        return;
    }

    if(app->marquee_phase == HA_REMOTE_MARQUEE_SCROLL) {
        if(app->marquee_offset < app->marquee_max_offset) {
            app->marquee_offset++;
        } else {
            app->marquee_phase = HA_REMOTE_MARQUEE_HOLD_END;
            app->marquee_tick = 0;
        }
    } else if(app->marquee_phase == HA_REMOTE_MARQUEE_HOLD_END) {
        if(++app->marquee_tick >= HA_REMOTE_MARQUEE_HOLD_TICKS) {
            app->marquee_phase = HA_REMOTE_MARQUEE_SCROLL_BACK;
            app->marquee_tick = 0;
        }
    } else if(app->marquee_phase == HA_REMOTE_MARQUEE_SCROLL_BACK) {
        if(app->marquee_offset > 0) {
            app->marquee_offset--;
        } else {
            app->marquee_phase = HA_REMOTE_MARQUEE_HOLD_START;
            app->marquee_tick = 0;
        }
    } else {
        if(++app->marquee_tick >= HA_REMOTE_MARQUEE_HOLD_TICKS) {
            app->marquee_phase = HA_REMOTE_MARQUEE_SCROLL;
            app->marquee_tick = 0;
        }
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, HaRemoteCustomEventMarqueeTick);
}

static Submenu* ha_remote_current_menu(HaRemoteApp* app) {
    switch(app->current_view) {
    case HA_REMOTE_VIEW_SETTINGS:
        return app->settings_menu;
    case HA_REMOTE_VIEW_ABOUT:
        return app->about_menu;
    case HA_REMOTE_VIEW_ROOT:
    default:
        return NULL;
    }
}

static const char* ha_remote_current_default_header(HaRemoteApp* app) {
    switch(app->current_view) {
    case HA_REMOTE_VIEW_CONTROLLER:
        return "Controller";
    case HA_REMOTE_VIEW_THERMOSTAT:
        return "Thermostat";
    case HA_REMOTE_VIEW_DIMMER:
        return "Dimmer";
    case HA_REMOTE_VIEW_CONTROLLER_INFO:
        return "Info";
    case HA_REMOTE_VIEW_ADD:
    case HA_REMOTE_VIEW_ADD_TEXT:
        return "Add";
    case HA_REMOTE_VIEW_SETTINGS:
        return "Settings";
    case HA_REMOTE_VIEW_ABOUT:
        return "About";
    case HA_REMOTE_VIEW_ROOT:
    default:
        return "FlipperHA";
    }
}

static bool ha_remote_action_has_state(const HaRemoteAction* action) {
    return action->kind != HaRemoteActionKindRun;
}

static bool ha_remote_action_is_dimmer(const HaRemoteAction* action) {
    return action->kind == HaRemoteActionKindDimmer;
}

static bool ha_remote_entry_is_dimmer(HaRemoteApp* app, uint8_t entry_id) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        return ha_remote_action_is_dimmer(&ha_remote_actions[entry_id]);
    }

    uint8_t custom_index;
    return ha_remote_custom_index_from_entry(app, entry_id, &custom_index) &&
           app->custom_entries[custom_index].kind == HaRemoteCustomKindDimmer;
}

static bool ha_remote_entry_has_state(HaRemoteApp* app, uint8_t entry_id) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        return ha_remote_action_has_state(&ha_remote_actions[entry_id]);
    }

    uint8_t custom_index;
    return ha_remote_custom_index_from_entry(app, entry_id, &custom_index) &&
           app->custom_entries[custom_index].kind != HaRemoteCustomKindRoutine;
}

static const char* ha_remote_entry_label(HaRemoteApp* app, uint8_t entry_id) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        return ha_remote_actions[entry_id].label;
    }

    uint8_t custom_index;
    if(ha_remote_custom_index_from_entry(app, entry_id, &custom_index)) {
        return app->custom_entries[custom_index].label;
    }

    return "Controller";
}

static const char* ha_remote_entry_state(HaRemoteApp* app, uint8_t entry_id, bool* known) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        if(known) {
            *known = app->action_state_known[entry_id];
        }
        return app->action_states[entry_id];
    }

    uint8_t custom_index;
    if(ha_remote_custom_index_from_entry(app, entry_id, &custom_index)) {
        if(known) {
            *known = app->custom_state_known[custom_index];
        }
        return app->custom_states[custom_index];
    }

    if(known) {
        *known = false;
    }
    return "--";
}

static void ha_remote_format_entry_state(
    HaRemoteApp* app,
    uint8_t entry_id,
    const char* value,
    char* out,
    size_t out_size) {
    if(ha_remote_entry_is_dimmer(app, entry_id) && ha_remote_state_is_off(value)) {
        snprintf(out, out_size, "OFF");
        return;
    }

    if(ha_remote_value_equals(value, "on")) {
        snprintf(out, out_size, "On");
        return;
    }

    if(ha_remote_value_equals(value, "off")) {
        snprintf(out, out_size, "Off");
        return;
    }

    snprintf(out, out_size, "%s", value && value[0] ? value : "--");
}

static bool ha_remote_log_success_enabled(HaRemoteApp* app) {
    return app->log_mode == HaRemoteLogModeLight || app->log_mode == HaRemoteLogModeDebug;
}

static bool ha_remote_log_error_enabled(HaRemoteApp* app) {
    return app->log_mode != HaRemoteLogModeOff;
}

static bool ha_remote_log_debug_enabled(HaRemoteApp* app) {
    return app->log_mode == HaRemoteLogModeDebug;
}

static uint8_t ha_remote_controller_total_entry_count(HaRemoteApp* app) {
    return HA_REMOTE_ACTION_COUNT + app->custom_entry_count;
}

static bool ha_remote_entry_is_custom(uint8_t entry_id) {
    return !ha_remote_entry_is_builtin(entry_id);
}

static bool ha_remote_custom_index_from_entry(
    HaRemoteApp* app,
    uint8_t entry_id,
    uint8_t* custom_index) {
    if(!ha_remote_entry_is_custom(entry_id)) {
        return false;
    }

    uint8_t index = entry_id - HA_REMOTE_ACTION_COUNT;
    if(index >= app->custom_entry_count || index >= HA_REMOTE_CUSTOM_ENTRY_MAX ||
       !app->custom_entries[index].used) {
        return false;
    }

    if(custom_index) {
        *custom_index = index;
    }
    return true;
}

static bool ha_remote_entry_id_is_valid(HaRemoteApp* app, uint8_t entry_id) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        return true;
    }
    return ha_remote_custom_index_from_entry(app, entry_id, NULL);
}

static void ha_remote_controller_order_reset(HaRemoteApp* app) {
    app->action_order_count = HA_REMOTE_ACTION_COUNT;
    app->custom_entry_count = 0;
    memset(app->custom_entries, 0, sizeof(app->custom_entries));
    memset(app->custom_states, 0, sizeof(app->custom_states));
    memset(app->custom_state_known, 0, sizeof(app->custom_state_known));
#if HA_REMOTE_ACTION_COUNT > 0
    for(uint8_t i = 0; i < HA_REMOTE_ACTION_COUNT; i++) {
        app->action_order[i] = i;
    }
#endif
    for(uint8_t i = HA_REMOTE_ACTION_COUNT; i < HA_REMOTE_CONTROLLER_ENTRY_MAX; i++) {
        app->action_order[i] = 0;
    }
}

static bool ha_remote_legacy_order_is_valid(const uint8_t* order, uint8_t count) {
#if HA_REMOTE_ACTION_COUNT > 0
    if(count > HA_REMOTE_ACTION_COUNT) {
        return false;
    }

    uint32_t seen = 0;

    for(uint8_t i = 0; i < HA_REMOTE_ACTION_COUNT; i++) {
        uint8_t action_index = order[i];
        if(action_index >= HA_REMOTE_ACTION_COUNT) {
            return false;
        }

        uint32_t mask = 1UL << action_index;
        if(seen & mask) {
            return false;
        }
        seen |= mask;
    }

    return seen == ((1UL << HA_REMOTE_ACTION_COUNT) - 1);
#else
    UNUSED(order);
    return count == 0;
#endif
}

static bool
    ha_remote_controller_order_is_valid(const uint8_t* order, uint8_t visible_count, uint8_t total_count) {
#if HA_REMOTE_ACTION_COUNT > 0
    if(total_count < HA_REMOTE_ACTION_COUNT || total_count > HA_REMOTE_CONTROLLER_ENTRY_MAX ||
       visible_count > total_count) {
        return false;
    }
#else
    if(total_count > HA_REMOTE_CONTROLLER_ENTRY_MAX || visible_count > total_count) {
        return false;
    }
#endif

    uint32_t seen = 0;
    for(uint8_t i = 0; i < total_count; i++) {
        uint8_t entry_id = order[i];
        if(entry_id >= total_count) {
            return false;
        }

        uint32_t mask = 1UL << entry_id;
        if(seen & mask) {
            return false;
        }
        seen |= mask;
    }

    uint32_t expected = total_count >= 32 ? 0xFFFFFFFFUL : ((1UL << total_count) - 1);
    return seen == expected;
}

static uint8_t ha_remote_controller_item_count(HaRemoteApp* app) {
    return HA_REMOTE_ACTION_MENU_BASE + app->action_order_count;
}

static bool ha_remote_controller_entry_id_for_item(
    HaRemoteApp* app,
    uint8_t item_index,
    uint8_t* entry_id) {
    if(item_index <= HA_REMOTE_REFRESH_INDEX || item_index >= ha_remote_controller_item_count(app)) {
        return false;
    }

    uint8_t order_slot = item_index - HA_REMOTE_ACTION_MENU_BASE;
    if(order_slot >= app->action_order_count) {
        return false;
    }

    uint8_t candidate = app->action_order[order_slot];
    if(!ha_remote_entry_id_is_valid(app, candidate)) {
        return false;
    }

    *entry_id = candidate;
    return true;
}

static bool ha_remote_controller_action_index_for_item(
    HaRemoteApp* app,
    uint8_t item_index,
    uint8_t* action_index) {
    if(item_index <= HA_REMOTE_REFRESH_INDEX || item_index >= ha_remote_controller_item_count(app)) {
        return false;
    }

    uint8_t entry_id;
    if(!ha_remote_controller_entry_id_for_item(app, item_index, &entry_id) ||
       !ha_remote_entry_is_builtin(entry_id)) {
        return false;
    }

    *action_index = entry_id;
    return true;
}

static void ha_remote_controller_order_load(HaRemoteApp* app) {
    ha_remote_controller_order_reset(app);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_ENTRY_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        HaRemoteControllerEntryFile data;
        if(storage_file_read(file, &data, sizeof(data)) == sizeof(data) &&
           data.magic == HA_REMOTE_ENTRY_MAGIC && data.version == HA_REMOTE_ENTRY_VERSION &&
           data.custom_count <= HA_REMOTE_CUSTOM_ENTRY_MAX &&
           data.total_count == HA_REMOTE_ACTION_COUNT + data.custom_count &&
           ha_remote_controller_order_is_valid(data.order, data.visible_count, data.total_count)) {
            memcpy(app->action_order, data.order, sizeof(app->action_order));
            memcpy(app->custom_entries, data.custom, sizeof(app->custom_entries));
            app->action_order_count = data.visible_count;
            app->custom_entry_count = data.custom_count;
            for(uint8_t i = 0; i < app->custom_entry_count; i++) {
                app->custom_entries[i].used = 1;
            }
            storage_file_close(file);
            storage_file_free(file);
            furi_string_free(path);
            furi_record_close(RECORD_STORAGE);
            return;
        }
    }
    storage_file_close(file);

    furi_string_set(path, HA_REMOTE_ORDER_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);
    ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        HaRemoteControllerOrderFile legacy;
        if(storage_file_read(file, &legacy, sizeof(legacy)) == sizeof(legacy) &&
           legacy.magic == HA_REMOTE_ORDER_MAGIC && legacy.version == HA_REMOTE_ORDER_VERSION &&
           ha_remote_legacy_order_is_valid(legacy.order, legacy.count)) {
            memcpy(app->action_order, legacy.order, sizeof(legacy.order));
            app->action_order_count = legacy.count;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

static bool ha_remote_controller_order_save(HaRemoteApp* app) {
    uint8_t total_count = ha_remote_controller_total_entry_count(app);
    HaRemoteControllerEntryFile data = {
        .magic = HA_REMOTE_ENTRY_MAGIC,
        .version = HA_REMOTE_ENTRY_VERSION,
        .visible_count = app->action_order_count,
        .total_count = total_count,
        .custom_count = app->custom_entry_count,
    };
    memcpy(data.order, app->action_order, sizeof(data.order));
    memcpy(data.custom, app->custom_entries, sizeof(data.custom));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_ENTRY_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        ok = storage_file_write(file, &data, sizeof(data)) == sizeof(data) &&
             storage_file_sync(file);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);

    return ok;
}

static void ha_remote_settings_save(HaRemoteApp* app) {
    HaRemoteSettingsFile data = {
        .magic = HA_REMOTE_SETTINGS_MAGIC,
        .version = HA_REMOTE_SETTINGS_VERSION,
        .log_mode = (uint8_t)app->log_mode,
        .vibration_enabled = app->vibration_enabled ? 1 : 0,
        .reserved = 0,
    };

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_SETTINGS_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &data, sizeof(data));
        storage_file_sync(file);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

static void ha_remote_settings_load(HaRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_SETTINGS_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        HaRemoteSettingsFile data;
        if(storage_file_read(file, &data, sizeof(data)) == sizeof(data) &&
           data.magic == HA_REMOTE_SETTINGS_MAGIC && data.version == HA_REMOTE_SETTINGS_VERSION) {
            if(data.log_mode < HaRemoteLogModeCount) {
                app->log_mode = (HaRemoteLogMode)data.log_mode;
            }
            app->vibration_enabled = data.vibration_enabled ? true : false;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

static void ha_remote_clear_pending_command(HaRemoteApp* app) {
    app->pending_command_type = HaRemotePendingCommandNone;
    app->pending_action_index = 0;
    app->pending_command_path[0] = '\0';
}

static void ha_remote_drop_pending_command(HaRemoteApp* app) {
    if(app->command_timer) {
        furi_timer_stop(app->command_timer);
    }
    ha_remote_clear_pending_command(app);
    if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
        ha_remote_controller_model_sync(app, true);
    }
}

static void ha_remote_root_model_sync(HaRemoteApp* app, bool update) {
    if(!app->root_view) {
        return;
    }

    HaRemoteRootModel* model = view_get_model(app->root_view);
    snprintf(model->header, sizeof(model->header), "%s", app->status_header);
    model->selected = app->root_selected;
    view_commit_model(app->root_view, update);
}

static void ha_remote_write_header(HaRemoteApp* app, const char* header) {
    if(!header) {
        header = "";
    }

    snprintf(app->status_header, sizeof(app->status_header), "%s", header);

    if(app->current_view == HA_REMOTE_VIEW_THERMOSTAT) {
        ha_remote_thermostat_set_status(app, app->status_header);
        return;
    }

    if(app->current_view == HA_REMOTE_VIEW_ROOT) {
        ha_remote_root_model_sync(app, true);
        return;
    }

    if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
        ha_remote_controller_model_sync(app, true);
        return;
    }

    Submenu* menu = ha_remote_current_menu(app);
    if(!menu) {
        return;
    }

    submenu_set_header(menu, app->status_header);
}

static void ha_remote_set_header(HaRemoteApp* app, const char* header, bool clear_later) {
    app->loading_enabled = false;
    ha_remote_write_header(app, header);
    app->status_clear_enabled = clear_later;

    if(clear_later) {
        furi_timer_restart(app->status_timer, HA_REMOTE_STATUS_TIMER_MS);
    } else {
        furi_timer_stop(app->status_timer);
    }
}

static void ha_remote_notify(HaRemoteApp* app, const NotificationSequence* sequence) {
    if(app->vibration_enabled) {
        notification_message(app->notifications, sequence);
    }
}

static void ha_remote_loading_update(HaRemoteApp* app, bool force) {
    static const char* blocks[] = {"[#---]", "[##--]", "[###-]", "[####]"};
    uint32_t now = furi_get_tick();

    if(!app->loading_enabled) {
        return;
    }

    if(!force &&
       app->loading_last_tick &&
       (now - app->loading_last_tick) < furi_ms_to_ticks(HA_REMOTE_LOADING_STEP_MS)) {
        return;
    }

    snprintf(
        app->status_header,
        sizeof(app->status_header),
        "%s %s",
        app->loading_label,
        blocks[app->loading_step]);
    if(app->current_view == HA_REMOTE_VIEW_THERMOSTAT) {
        ha_remote_thermostat_set_status(app, app->status_header);
    }
    if(app->current_view == HA_REMOTE_VIEW_ROOT) {
        ha_remote_root_model_sync(app, true);
    }
    if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
        ha_remote_controller_model_sync(app, true);
    }
    Submenu* menu = ha_remote_current_menu(app);
    if(menu) {
        submenu_set_header(menu, app->status_header);
    }
    app->loading_step = (app->loading_step + 1) % COUNT_OF(blocks);
    app->loading_last_tick = now;
}

static void ha_remote_loading_start(HaRemoteApp* app, const char* label) {
    app->loading_enabled = true;
    app->loading_label = label;
    app->loading_step = 0;
    app->loading_last_tick = 0;
    app->status_clear_enabled = false;
    furi_timer_stop(app->status_timer);
    ha_remote_loading_update(app, true);
}

static void ha_remote_loading_delay(HaRemoteApp* app, uint32_t delay_ms) {
    uint32_t started = furi_get_tick();
    uint32_t delay_ticks = furi_ms_to_ticks(delay_ms);

    while((furi_get_tick() - started) < delay_ticks) {
        ha_remote_loading_update(app, false);
        furi_delay_ms(50);
    }
}

static bool ha_remote_custom_event_callback(void* context, uint32_t event) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event == HaRemoteCustomEventClearStatus) {
        if(app->status_clear_enabled) {
            app->status_clear_enabled = false;
            ha_remote_set_header(app, ha_remote_current_default_header(app), false);
        }
        return true;
    }

    if(event == HaRemoteCustomEventDispatchPendingCommand) {
        ha_remote_dispatch_pending_command(app);
        return true;
    }

    if(event == HaRemoteCustomEventControllerSyncComplete) {
        ha_remote_complete_controller_sync(app);
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER_INFO) {
            ha_remote_controller_info_model_sync(app, true);
        }
        return true;
    }

    if(event == HaRemoteCustomEventCommandComplete) {
        ha_remote_complete_command_results(app);
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER_INFO) {
            ha_remote_controller_info_model_sync(app, true);
        }
        return true;
    }

    if(event == HaRemoteCustomEventCommandStatusChanged) {
        ha_remote_controller_model_sync(app, true);
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER_INFO) {
            ha_remote_controller_info_model_sync(app, true);
        }
        return true;
    }

    if(event == HaRemoteCustomEventAddLoadComplete) {
        ha_remote_complete_add_load(app);
        return true;
    }

    if(event == HaRemoteCustomEventMarqueeTick) {
        if(app->current_view == HA_REMOTE_VIEW_ADD && app->add_mode == HaRemoteAddModeInfo) {
            ha_remote_add_model_sync(app, true);
        }
        return true;
    }

    return false;
}

static bool ha_remote_command_queue_active(HaRemoteApp* app) {
    return app->command_worker_busy ||
           (app->command_queue && furi_message_queue_get_count(app->command_queue) > 0) ||
           (app->pending_command_type != HaRemotePendingCommandNone);
}

static void ha_remote_set_queue_header(HaRemoteApp* app) {
    uint32_t queued = app->command_queue ? furi_message_queue_get_count(app->command_queue) : 0;

    char header[HA_REMOTE_HEADER_MAX];
    if(queued > 1) {
        snprintf(header, sizeof(header), "Queued %lu", (unsigned long)queued);
    } else if(app->command_worker_busy && queued == 0) {
        snprintf(header, sizeof(header), "Sending");
    } else {
        snprintf(header, sizeof(header), "Queued");
    }
    ha_remote_set_header(app, header, true);
}

static bool ha_remote_enqueue_command(HaRemoteApp* app, const HaRemoteQueuedCommand* command) {
    if(!app->command_queue ||
       furi_message_queue_put(app->command_queue, command, 0) != FuriStatusOk) {
        ha_remote_set_header(app, "Queue Full", true);
        return false;
    }

    ha_remote_set_queue_header(app);
    return true;
}

static bool ha_remote_enqueue_pending_command(HaRemoteApp* app) {
    if(app->pending_command_type == HaRemotePendingCommandNone) {
        return true;
    }

    HaRemoteQueuedCommand command = {
        .type = app->pending_command_type,
        .action_index = app->pending_action_index,
    };
    snprintf(command.path, sizeof(command.path), "%s", app->pending_command_path);

    if(!ha_remote_enqueue_command(app, &command)) {
        furi_timer_start(app->command_timer, HA_REMOTE_LOADING_STEP_MS);
        return false;
    }

    ha_remote_drop_pending_command(app);
    return true;
}

static bool ha_remote_dispatch_pending_command(HaRemoteApp* app) {
    return ha_remote_enqueue_pending_command(app);
}

static bool ha_remote_cancel_pending_command(HaRemoteApp* app, const char* header) {
    if(app->pending_command_type == HaRemotePendingCommandNone) {
        return false;
    }

    ha_remote_drop_pending_command(app);
    ha_remote_set_header(app, header ? header : "Command Canceled", true);
    return true;
}

static void ha_remote_schedule_action_command(HaRemoteApp* app, uint8_t entry_id) {
    if(!ha_remote_entry_id_is_valid(app, entry_id) || !ha_remote_enqueue_pending_command(app)) {
        return;
    }

    app->pending_command_type = HaRemotePendingCommandAction;
    app->pending_action_index = entry_id;
    app->pending_command_path[0] = '\0';
    ha_remote_set_header(app, ha_remote_current_default_header(app), false);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, HaRemoteCustomEventDispatchPendingCommand);
}

static void ha_remote_schedule_dimmer_command(
    HaRemoteApp* app,
    uint8_t entry_id,
    uint8_t percent) {
    if(!ha_remote_entry_is_dimmer(app, entry_id) || !ha_remote_enqueue_pending_command(app)) {
        return;
    }

    HaRemoteQueuedCommand command = {
        .type = HaRemotePendingCommandDimmer,
        .action_index = entry_id,
    };
    if(ha_remote_entry_is_builtin(entry_id)) {
        snprintf(
            command.path,
            sizeof(command.path),
            "/dimmer/%02X/%u",
            ha_remote_actions[entry_id].action_id,
            (unsigned)percent);
    } else {
        uint8_t custom_index;
        if(!ha_remote_custom_index_from_entry(app, entry_id, &custom_index)) {
            return;
        }
        snprintf(
            command.path,
            sizeof(command.path),
            "/v1/entity/dimmer/%s/%u",
            app->custom_entries[custom_index].entity_id,
            (unsigned)percent);
    }
    ha_remote_enqueue_command(app, &command);
}

static void ha_remote_schedule_thermostat_command(HaRemoteApp* app, const char* path) {
    if(!ha_remote_enqueue_pending_command(app)) {
        return;
    }

    app->pending_command_type = HaRemotePendingCommandThermostat;
    app->pending_action_index = 0;
    snprintf(app->pending_command_path, sizeof(app->pending_command_path), "%s", path);
    ha_remote_set_header(app, ha_remote_current_default_header(app), false);
    view_dispatcher_send_custom_event(
        app->view_dispatcher, HaRemoteCustomEventDispatchPendingCommand);
}

static bool ha_remote_init_beacon_payload(HaRemoteApp* app) {
    size_t i = 0;

    app->beacon_payload[i++] = 2;
    app->beacon_payload[i++] = BTHOME_AD_TYPE_FLAGS;
    app->beacon_payload[i++] = 0x06;

    app->beacon_payload[i++] = 12;
    app->beacon_payload[i++] = BTHOME_AD_TYPE_SERVICE_DATA;
    app->beacon_payload[i++] = BTHOME_UUID_LSB;
    app->beacon_payload[i++] = BTHOME_UUID_MSB;
    app->beacon_payload[i++] = BTHOME_DEVICE_INFO_TRIGGER_V2;

    app->beacon_payload[i++] = BTHOME_OBJ_PACKET_ID;
    app->packet_id_offset = i;
    app->beacon_payload[i++] = 0x00;

    app->beacon_payload[i++] = BTHOME_OBJ_BATTERY;
    app->battery_offset = i;
    app->beacon_payload[i++] = 0x00;

    app->beacon_payload[i++] = BTHOME_OBJ_COMMAND;
    app->beacon_payload[i++] = BTHOME_COMMAND_ARG_LEN_1;
    app->beacon_payload[i++] = BTHOME_COMMAND_STEP_UP;
    app->command_arg_offset = i;
    app->beacon_payload[i++] = 0x00;

    const char* name = furi_hal_version_get_device_name_ptr();
    if(!name) {
        name = "Flipper";
    }

    size_t name_len = strlen(name);
    if(i + 2 >= EXTRA_BEACON_MAX_DATA_SIZE) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Not enough room for local name");
        return false;
    }

    if(i + 2 + name_len > EXTRA_BEACON_MAX_DATA_SIZE) {
        name_len = EXTRA_BEACON_MAX_DATA_SIZE - (i + 2);
        FURI_LOG_W(HA_REMOTE_LOG_TAG, "Truncated local name to %zu", name_len);
    }

    app->beacon_payload[i++] = 1 + name_len;
    app->beacon_payload[i++] = BTHOME_AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&app->beacon_payload[i], name, name_len);
    i += name_len;

    app->beacon_payload_len = i;
    return true;
}

static void ha_remote_send_ble_action(HaRemoteApp* app, const HaRemoteAction* action) {
    app->beacon_payload[app->packet_id_offset] = app->packet_id++;
    app->beacon_payload[app->battery_offset] = furi_hal_power_get_pct();
    app->beacon_payload[app->command_arg_offset] = action->action_id;

    if(furi_hal_bt_extra_beacon_is_active()) {
        furi_hal_bt_extra_beacon_stop();
    }

    furi_hal_bt_extra_beacon_set_data(app->beacon_payload, app->beacon_payload_len);

    if(furi_hal_bt_extra_beacon_start()) {
        furi_timer_start(app->beacon_timer, HA_REMOTE_BEACON_TIMER_MS);
        ha_remote_set_header(app, "Sent over BLE", true);
        ha_remote_notify(app, &sequence_single_vibro);
        if(ha_remote_log_success_enabled(app)) {
            FURI_LOG_I(
                HA_REMOTE_LOG_TAG,
                "Sent BLE action %s (0x%02X)",
                action->label,
                action->action_id);
        }
    } else {
        ha_remote_set_header(app, "BLE failed", true);
        ha_remote_notify(app, &sequence_error);
        if(ha_remote_log_error_enabled(app)) {
            FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to start extra beacon");
        }
    }
}

static void ha_remote_uart_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    FuriStreamBuffer* stream = context;
    if(!stream || !(event & FuriHalSerialRxEventData)) {
        return;
    }

    while(furi_hal_serial_async_rx_available(handle)) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(stream, &data, 1, 0);
    }
}

static bool ha_remote_uart_open(HaRemoteApp* app) {
    furi_assert(app);

    if(!app->uart_stream) {
        app->uart_stream = furi_stream_buffer_alloc(HA_REMOTE_UART_STREAM_SIZE, 1);
        if(!app->uart_stream) {
            FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate UART stream");
            return false;
        }
    }

    furi_stream_buffer_reset(app->uart_stream);

    if(app->uart_handle) {
        return true;
    }

    if(!app->expansion_opened) {
        app->expansion = furi_record_open(RECORD_EXPANSION);
    }
    if(app->expansion && !app->expansion_opened) {
        expansion_disable(app->expansion);
        app->expansion_opened = true;
        furi_delay_ms(HA_REMOTE_UART_READY_DELAY_MS);
    }

    if(furi_hal_serial_control_is_busy(FuriHalSerialIdUsart)) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "USART is busy");
        ha_remote_uart_close(app);
        return false;
    }

    app->uart_handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->uart_handle) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to acquire USART");
        ha_remote_uart_close(app);
        return false;
    }

    furi_hal_serial_init(app->uart_handle, HA_REMOTE_UART_BAUD);
    furi_hal_serial_enable_direction(app->uart_handle, FuriHalSerialDirectionRx);
    furi_hal_serial_enable_direction(app->uart_handle, FuriHalSerialDirectionTx);
    furi_hal_serial_async_rx_start(
        app->uart_handle, ha_remote_uart_rx_callback, app->uart_stream, false);
    furi_delay_ms(HA_REMOTE_UART_READY_DELAY_MS);

    return true;
}

static void ha_remote_uart_close(HaRemoteApp* app) {
    if(app->uart_handle) {
        furi_hal_serial_async_rx_stop(app->uart_handle);
        furi_hal_serial_disable_direction(app->uart_handle, FuriHalSerialDirectionRx);
        furi_hal_serial_disable_direction(app->uart_handle, FuriHalSerialDirectionTx);
        furi_hal_serial_deinit(app->uart_handle);
        furi_hal_serial_control_release(app->uart_handle);
        app->uart_handle = NULL;
    }

    if(app->expansion_opened && app->expansion) {
        expansion_enable(app->expansion);
        furi_record_close(RECORD_EXPANSION);
        app->expansion = NULL;
        app->expansion_opened = false;
    }
}

static bool ha_remote_uart_write_crlf(HaRemoteApp* app, const char* command) {
    size_t command_len = strlen(command);
    if(command_len == 0 || command_len + 2 > 512) {
        return false;
    }

    furi_hal_serial_tx(app->uart_handle, (const uint8_t*)command, command_len);
    furi_hal_serial_tx(app->uart_handle, (const uint8_t*)"\r\n", 2);
    furi_hal_serial_tx_wait_complete(app->uart_handle);
    return true;
}

static bool ha_remote_ping_board(
    HaRemoteApp* app,
    char* response,
    size_t response_size,
    bool loading) {
    for(uint8_t attempt = 0; attempt < HA_REMOTE_UART_PING_RETRIES; attempt++) {
        furi_stream_buffer_reset(app->uart_stream);
        ha_remote_uart_write_crlf(app, "[PING]");
        if(ha_remote_uart_read_window(
               app,
               response,
               response_size,
               HA_REMOTE_UART_PING_TIMEOUT_MS,
               "[PONG]",
               loading)) {
            return true;
        }
        if(loading) {
            ha_remote_loading_delay(app, HA_REMOTE_UART_PING_RETRY_DELAY_MS);
        } else {
            furi_delay_ms(HA_REMOTE_UART_PING_RETRY_DELAY_MS);
        }
    }

    return false;
}

static bool ha_remote_uart_read_window(
    HaRemoteApp* app,
    char* out,
    size_t out_size,
    uint32_t timeout_ms,
    const char* stop_marker,
    bool loading) {
    furi_assert(out);
    furi_assert(out_size > 0);

    out[0] = '\0';
    size_t out_len = 0;
    uint32_t started = furi_get_tick();
    uint32_t timeout_ticks = furi_ms_to_ticks(timeout_ms);

    while((furi_get_tick() - started) < timeout_ticks && out_len < out_size - 1) {
        uint8_t chunk[64];
        if(loading) {
            ha_remote_loading_update(app, false);
        }
        size_t received = furi_stream_buffer_receive(app->uart_stream, chunk, sizeof(chunk), 100);
        if(received == 0) {
            continue;
        }

        size_t room = (out_size - 1) - out_len;
        if(received > room) {
            received = room;
        }
        memcpy(&out[out_len], chunk, received);
        out_len += received;
        out[out_len] = '\0';

        if(stop_marker && strstr(out, stop_marker)) {
            return true;
        }
    }

    return out_len > 0 && (!stop_marker || strstr(out, stop_marker));
}

static void ha_remote_copy_trimmed(char* out, size_t out_size, const char* start, size_t len) {
    while(len > 0 && (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t')) {
        start++;
        len--;
    }

    while(len > 0) {
        char last = start[len - 1];
        if(last != ' ' && last != '\r' && last != '\n' && last != '\t') {
            break;
        }
        len--;
    }

    if(len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, start, len);
    out[len] = '\0';
}

static bool ha_remote_bridge_config_values_ready(const char* base_url, const char* key) {
    return base_url[0] && key[0] && strcmp(base_url, HA_REMOTE_DEFAULT_BRIDGE_BASE_URL) != 0 &&
           strcmp(key, HA_REMOTE_DEFAULT_BRIDGE_KEY) != 0;
}

static void ha_remote_bridge_config_set_defaults(HaRemoteApp* app) {
    ha_remote_copy_trimmed(
        app->bridge_base_url,
        sizeof(app->bridge_base_url),
        FLIPPERHA_BRIDGE_BASE_URL,
        strlen(FLIPPERHA_BRIDGE_BASE_URL));
    ha_remote_copy_trimmed(
        app->bridge_key,
        sizeof(app->bridge_key),
        FLIPPERHA_BRIDGE_KEY,
        strlen(FLIPPERHA_BRIDGE_KEY));
    app->bridge_config_from_file = false;
    app->bridge_configured =
        ha_remote_bridge_config_values_ready(app->bridge_base_url, app->bridge_key);
}

static bool ha_remote_bridge_config_apply_line(HaRemoteApp* app, const char* start, size_t len) {
    while(len > 0 && (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t')) {
        start++;
        len--;
    }

    if(len == 0 || *start == '#') {
        return false;
    }

    const char* separator = memchr(start, '=', len);
    if(!separator) {
        return false;
    }

    char name[20];
    ha_remote_copy_trimmed(name, sizeof(name), start, (size_t)(separator - start));
    const char* value = separator + 1;
    size_t value_len = len - (size_t)(value - start);

    if(strcmp(name, "url") == 0 || strcmp(name, "base_url") == 0) {
        ha_remote_copy_trimmed(app->bridge_base_url, sizeof(app->bridge_base_url), value, value_len);
        return true;
    } else if(strcmp(name, "key") == 0 || strcmp(name, "bridge_key") == 0) {
        ha_remote_copy_trimmed(app->bridge_key, sizeof(app->bridge_key), value, value_len);
        return true;
    } else if(
        strcmp(name, "thermostat") == 0 || strcmp(name, "thermostat_entity") == 0 ||
        strcmp(name, "climate") == 0) {
        char entity_id[HA_REMOTE_ENTITY_ID_MAX];
        ha_remote_copy_trimmed(entity_id, sizeof(entity_id), value, value_len);
        if(strncmp(entity_id, "climate.", 8) == 0) {
            snprintf(app->thermostat_entity_id, sizeof(app->thermostat_entity_id), "%s", entity_id);
            app->thermostat_configured = true;
        }
    }

    return false;
}

static void ha_remote_bridge_config_load(HaRemoteApp* app) {
    ha_remote_bridge_config_set_defaults(app);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_BRIDGE_CONFIG_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    bool loaded = false;
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[384];
        size_t bytes = storage_file_read(file, buffer, sizeof(buffer) - 1);
        buffer[bytes] = '\0';

        const char* cursor = buffer;
        const char* end = buffer + bytes;
        while(cursor < end) {
            const char* line_end = cursor;
            while(line_end < end && *line_end != '\n') {
                line_end++;
            }
            size_t line_len = (size_t)(line_end - cursor);
            if(ha_remote_bridge_config_apply_line(app, cursor, line_len)) {
                loaded = true;
            }
            cursor = line_end < end ? line_end + 1 : line_end;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);

    app->bridge_configured =
        ha_remote_bridge_config_values_ready(app->bridge_base_url, app->bridge_key);
    app->bridge_config_from_file = loaded && app->bridge_configured;
}

static void ha_remote_thermostat_config_apply_line(HaRemoteApp* app, const char* start, size_t len) {
    while(len > 0 && (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t')) {
        start++;
        len--;
    }

    if(len == 0 || *start == '#') {
        return;
    }

    const char* separator = memchr(start, '=', len);
    if(!separator) {
        return;
    }

    char name[20];
    ha_remote_copy_trimmed(name, sizeof(name), start, (size_t)(separator - start));
    const char* value = separator + 1;
    size_t value_len = len - (size_t)(value - start);

    if(
        strcmp(name, "entity_id") == 0 || strcmp(name, "thermostat") == 0 ||
        strcmp(name, "thermostat_entity") == 0 || strcmp(name, "climate") == 0) {
        char entity_id[HA_REMOTE_ENTITY_ID_MAX];
        ha_remote_copy_trimmed(entity_id, sizeof(entity_id), value, value_len);
        if(strncmp(entity_id, "climate.", 8) == 0) {
            snprintf(app->thermostat_entity_id, sizeof(app->thermostat_entity_id), "%s", entity_id);
            app->thermostat_configured = true;
        }
    }
}

static void ha_remote_thermostat_config_load(HaRemoteApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_THERMOSTAT_CONFIG_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buffer[160];
        size_t bytes = storage_file_read(file, buffer, sizeof(buffer) - 1);
        buffer[bytes] = '\0';

        const char* cursor = buffer;
        const char* end = buffer + bytes;
        while(cursor < end) {
            const char* line_end = cursor;
            while(line_end < end && *line_end != '\n') {
                line_end++;
            }
            ha_remote_thermostat_config_apply_line(app, cursor, (size_t)(line_end - cursor));
            cursor = line_end < end ? line_end + 1 : line_end;
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
}

static bool ha_remote_thermostat_config_save(HaRemoteApp* app) {
    if(!app->thermostat_configured || !app->thermostat_entity_id[0]) {
        return false;
    }

    char body[96];
    int written = snprintf(body, sizeof(body), "entity_id=%s\n", app->thermostat_entity_id);
    if(written < 0 || written >= (int)sizeof(body)) {
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FuriString* path = furi_string_alloc_set_str(HA_REMOTE_THERMOSTAT_CONFIG_FILE);
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        ok = storage_file_write(file, body, strlen(body)) == strlen(body) && storage_file_sync(file);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static bool ha_remote_extract_get_body(const char* response, char* body, size_t body_size) {
    if(!strstr(response, "[GET/SUCCESS]")) {
        ha_remote_copy_trimmed(body, body_size, response, strlen(response));
        return false;
    }

    const char* start = strstr(response, "}\r\n");
    if(start) {
        start += 3;
    } else {
        start = strstr(response, "}\n");
        if(start) {
            start += 2;
        } else {
            start = strchr(response, '}');
            if(start) {
                start++;
            } else {
                start = response;
            }
        }
    }

    const char* end = strstr(start, "[GET/END]");
    size_t len = end ? (size_t)(end - start) : strlen(start);
    ha_remote_copy_trimmed(body, body_size, start, len);
    return body[0] != '\0';
}

static bool ha_remote_bridge_get_with_buffers(
    HaRemoteApp* app,
    const char* path,
    char* command,
    size_t command_size,
    char* response,
    size_t response_size,
    char* body,
    size_t body_size,
    bool loading) {
    bool ok = false;
    bool bridge_locked = false;
    body[0] = '\0';
    const char* request_path = path;
    if(!app->bridge_configured) {
        snprintf(body, body_size, "setup needed");
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s skipped: setup needed", request_path);
        }
        return false;
    }

    const char* base = app->bridge_base_url;
    size_t base_len = strlen(base);

    if(base_len >= 3 && strcmp(base + base_len - 3, "/v1") == 0 &&
       strncmp(path, "/v1/", 4) == 0) {
        request_path = path + 3;
    }

    if(ha_remote_log_debug_enabled(app)) {
        FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s", request_path);
    }

    if(app->bridge_mutex) {
        FuriStatus lock_status =
            furi_mutex_acquire(app->bridge_mutex, loading ? 0 : FuriWaitForever);
        if(lock_status != FuriStatusOk) {
            snprintf(body, body_size, "bridge busy");
            if(ha_remote_log_debug_enabled(app)) {
                FURI_LOG_D(
                    HA_REMOTE_LOG_TAG,
                    "Bridge GET %s failed: bridge busy",
                    request_path);
            }
            return false;
        }
        bridge_locked = true;
    }

    if(!ha_remote_uart_open(app)) {
        ha_remote_uart_close(app);
        snprintf(body, body_size, "uart busy");
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s failed: uart busy", request_path);
        }
        goto done;
    }

    if(!ha_remote_ping_board(app, response, response_size, loading)) {
        snprintf(body, body_size, "no board");
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s failed: no board", request_path);
        }
        goto done;
    }

    furi_stream_buffer_reset(app->uart_stream);
    int written = snprintf(
        command,
        command_size,
        "[GET]%s%s?k=%s",
        app->bridge_base_url,
        request_path,
        app->bridge_key);
    if(written < 0 || written >= (int)command_size) {
        snprintf(body, body_size, "url long");
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s failed: url long", request_path);
        }
        goto done;
    }

    ha_remote_uart_write_crlf(app, command);
    ok = ha_remote_uart_read_window(app, response, response_size, 8000, "[GET/END]", loading);

    if(!ok) {
        snprintf(body, body_size, "get timeout");
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Bridge GET %s failed: get timeout", request_path);
        }
        goto done;
    }

    ok = ha_remote_extract_get_body(response, body, body_size);
    if(ha_remote_log_debug_enabled(app)) {
        FURI_LOG_D(
            HA_REMOTE_LOG_TAG,
            "Bridge GET %s -> %s",
            request_path,
            ok && body[0] ? body : "empty response");
    }
done:
    if(bridge_locked) {
        furi_mutex_release(app->bridge_mutex);
    }
    return ok;
}

static bool ha_remote_bridge_get(HaRemoteApp* app, const char* path, char* body, size_t body_size) {
    return ha_remote_bridge_get_with_buffers(
        app,
        path,
        app->http_command,
        sizeof(app->http_command),
        app->http_response,
        sizeof(app->http_response),
        body,
        body_size,
        true);
}

static bool ha_remote_command_build_path(
    HaRemoteApp* app,
    const HaRemoteQueuedCommand* command,
    char* path,
    size_t path_size) {
    if(command->type == HaRemotePendingCommandAction) {
        if(ha_remote_entry_is_builtin(command->action_index)) {
            snprintf(
                path,
                path_size,
                "/action/%02X",
                ha_remote_actions[command->action_index].action_id);
            return true;
        }

        uint8_t custom_index;
        if(!ha_remote_custom_index_from_entry(app, command->action_index, &custom_index)) {
            return false;
        }

        const HaRemoteCustomEntry* entry = &app->custom_entries[custom_index];
        const char* command_name =
            entry->kind == HaRemoteCustomKindRoutine ? "run" : "toggle";
        snprintf(path, path_size, "/v1/entity/%s/%s", command_name, entry->entity_id);
        return true;
    }

    if(command->type == HaRemotePendingCommandDimmer ||
       command->type == HaRemotePendingCommandThermostat) {
        if(!command->path[0]) {
            return false;
        }
        snprintf(path, path_size, "%s", command->path);
        return true;
    }

    return false;
}

static const char* ha_remote_command_success_header(HaRemotePendingCommandType type) {
    return type == HaRemotePendingCommandDimmer ? "Dimmer Set" : "Command Out";
}

static bool ha_remote_command_result_parse(HaRemoteApp* app, const HaRemoteCommandResult* result) {
    if(!result->ok) {
        return false;
    }

    if(result->type == HaRemotePendingCommandAction || result->type == HaRemotePendingCommandDimmer) {
        uint8_t custom_index;
        if(ha_remote_custom_index_from_entry(app, result->action_index, &custom_index)) {
            if(app->custom_entries[custom_index].kind == HaRemoteCustomKindRoutine) {
                snprintf(
                    app->custom_states[custom_index],
                    sizeof(app->custom_states[custom_index]),
                    "Run");
            } else {
                const char* state = result->body[0] ? result->body : "--";
                ha_remote_copy_trimmed(
                    app->custom_states[custom_index],
                    sizeof(app->custom_states[custom_index]),
                    state,
                    strlen(state));
            }
            app->custom_state_known[custom_index] = true;
            ha_remote_update_labels(app);
            return true;
        }
        return ha_remote_parse_states(app, result->body);
    }

    if(result->type == HaRemotePendingCommandThermostat) {
        return ha_remote_parse_thermostat(app, result->body);
    }

    return false;
}

static void ha_remote_complete_command_results(HaRemoteApp* app) {
    HaRemoteCommandResult result;
    bool any = false;
    bool failed = false;
    uint8_t success_count = 0;
    HaRemotePendingCommandType last_success_type = HaRemotePendingCommandNone;

    while(app->command_result_queue &&
          furi_message_queue_get(app->command_result_queue, &result, 0) == FuriStatusOk) {
        any = true;
        if(ha_remote_command_result_parse(app, &result)) {
            success_count++;
            last_success_type = result.type;
        } else {
            failed = true;
        }
    }

    if(!any) {
        return;
    }

    if(failed) {
        ha_remote_set_header(app, "Command Failed", true);
        ha_remote_notify(app, &sequence_error);
    } else if(success_count > 1) {
        ha_remote_set_header(app, "Queue Done", true);
        ha_remote_notify(app, &sequence_single_vibro);
    } else if(success_count == 1) {
        ha_remote_set_header(app, ha_remote_command_success_header(last_success_type), true);
        ha_remote_notify(app, &sequence_single_vibro);
    }
}

static int32_t ha_remote_command_worker(void* context) {
    HaRemoteApp* app = context;
    HaRemoteQueuedCommand command;
    char path[sizeof(command.path)];
    char http_command[HA_REMOTE_HTTP_COMMAND_MAX];
    char http_response[HA_REMOTE_HTTP_RESPONSE_MAX];
    char body[HA_REMOTE_HTTP_BODY_MAX];

    while(furi_message_queue_get(app->command_queue, &command, FuriWaitForever) == FuriStatusOk) {
        if(command.type == HaRemotePendingCommandStop) {
            break;
        }

        HaRemoteCommandResult result = {
            .type = command.type,
            .action_index = command.action_index,
            .ok = false,
        };

        if(ha_remote_command_build_path(app, &command, path, sizeof(path))) {
            snprintf(result.path, sizeof(result.path), "%s", path);
            app->command_worker_type = command.type;
            app->command_worker_action_index = command.action_index;
            app->command_worker_busy = true;
            view_dispatcher_send_custom_event(
                app->view_dispatcher, HaRemoteCustomEventCommandStatusChanged);
            result.ok = ha_remote_bridge_get_with_buffers(
                app,
                path,
                http_command,
                sizeof(http_command),
                http_response,
                sizeof(http_response),
                body,
                sizeof(body),
                false);
            app->command_worker_busy = false;
            app->command_worker_type = HaRemotePendingCommandNone;
            app->command_worker_action_index = 0;
            snprintf(result.body, sizeof(result.body), "%s", body);
        } else {
            snprintf(result.body, sizeof(result.body), "bad command");
        }

        if(ha_remote_log_success_enabled(app) && result.ok) {
            FURI_LOG_I(HA_REMOTE_LOG_TAG, "Queued command sent: %s", result.path);
        } else if(ha_remote_log_error_enabled(app) && !result.ok) {
            FURI_LOG_W(
                HA_REMOTE_LOG_TAG,
                "Queued command failed: %s (%s)",
                result.path[0] ? result.path : "unknown",
                result.body[0] ? result.body : "unknown error");
        }

        if(app->command_result_queue) {
            furi_message_queue_put(app->command_result_queue, &result, FuriWaitForever);
            view_dispatcher_send_custom_event(
                app->view_dispatcher, HaRemoteCustomEventCommandComplete);
        }
    }

    app->command_worker_busy = false;
    return 0;
}

// Pull one row's state out of a batched "|id=state|id=state" response.
static bool ha_remote_batch_state(
    const char* body,
    const char* entity_id,
    char* out,
    size_t out_size) {
    char needle[HA_REMOTE_ENTITY_ID_MAX + 3];
    snprintf(needle, sizeof(needle), "|%s=", entity_id);
    const char* found = strstr(body, needle);
    if(!found) {
        return false;
    }
    found += strlen(needle);
    size_t i = 0;
    while(found[i] && found[i] != '|' && i + 1 < out_size) {
        out[i] = found[i];
        i++;
    }
    out[i] = '\0';
    return true;
}

static int32_t ha_remote_controller_sync_worker(void* context) {
    HaRemoteApp* app = context;
    char command[HA_REMOTE_HTTP_COMMAND_MAX];
    char response[HA_REMOTE_HTTP_RESPONSE_MAX];
    char body[HA_REMOTE_HTTP_BODY_MAX];
    char path[HA_REMOTE_COMMAND_PATH_MAX];

    // Build ONE request for every switch/light row (routines need no state fetch).
    int written = snprintf(path, sizeof(path), "/v1/entity/states/");
    size_t path_len = written > 0 ? (size_t)written : 0;
    bool any_fetch = false;
    for(uint8_t i = 0; i < app->custom_entry_count; i++) {
        if(!app->custom_entries[i].used ||
           app->custom_entries[i].kind == HaRemoteCustomKindRoutine) {
            continue;
        }
        const char* eid = app->custom_entries[i].entity_id;
        size_t need = strlen(eid) + (any_fetch ? 1u : 0u);
        if(path_len + need >= HA_REMOTE_SYNC_PATH_BUDGET) {
            break;
        }
        if(any_fetch) {
            path[path_len++] = ',';
        }
        int w = snprintf(path + path_len, sizeof(path) - path_len, "%s", eid);
        if(w > 0) {
            path_len += (size_t)w;
        }
        any_fetch = true;
    }

    bool ok = true;
    body[0] = '\0';
    if(any_fetch) {
        ok = ha_remote_bridge_get_with_buffers(
            app,
            path,
            command,
            sizeof(command),
            response,
            sizeof(response),
            body,
            sizeof(body),
            false);
    }

    snprintf(app->controller_sync_body, sizeof(app->controller_sync_body), "%s", body);

    for(uint8_t i = 0; i < app->custom_entry_count; i++) {
        if(!app->custom_entries[i].used) {
            continue;
        }

        if(app->custom_entries[i].kind == HaRemoteCustomKindRoutine) {
            snprintf(app->custom_states[i], sizeof(app->custom_states[i]), "Run");
            app->custom_state_known[i] = true;
            continue;
        }

        if(ok && ha_remote_batch_state(
                     body,
                     app->custom_entries[i].entity_id,
                     app->custom_states[i],
                     sizeof(app->custom_states[i]))) {
            app->custom_state_known[i] = true;
        } else {
            app->custom_state_known[i] = false;
        }
    }

    app->controller_sync_ok = ok;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, HaRemoteCustomEventControllerSyncComplete);
    return 0;
}

static void ha_remote_join_controller_sync(HaRemoteApp* app) {
    if(app->controller_sync_thread) {
        furi_thread_join(app->controller_sync_thread);
        furi_thread_free(app->controller_sync_thread);
        app->controller_sync_thread = NULL;
    }
    app->controller_sync_running = false;
}

static bool ha_remote_start_controller_sync(HaRemoteApp* app, bool notify) {
    if(app->action_order_count == 0) {
        app->controller_sync_body[0] = '\0';
        app->controller_sync_ok = false;
        app->controller_sync_notify = false;
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
            ha_remote_set_header(app, "No rows yet", true);
            ha_remote_controller_model_sync(app, true);
        }
        UNUSED(notify);
        return false;
    }
    if(app->controller_sync_running) {
        app->controller_sync_notify = app->controller_sync_notify || notify;
        ha_remote_set_header(app, "Syncing...", false);
        return false;
    }

    app->controller_sync_body[0] = '\0';
    app->controller_sync_ok = false;
    app->controller_sync_notify = notify;
    app->controller_sync_thread =
        furi_thread_alloc_ex("HA Controller Sync", 6144, ha_remote_controller_sync_worker, app);
    if(!app->controller_sync_thread) {
        ha_remote_set_header(app, "Sync Failed", true);
        if(notify) {
            ha_remote_notify(app, &sequence_error);
        }
        return false;
    }

    app->controller_sync_running = true;
    ha_remote_set_header(app, "Syncing...", false);
    furi_thread_start(app->controller_sync_thread);
    return true;
}

static void ha_remote_complete_controller_sync(HaRemoteApp* app) {
    bool ok = app->controller_sync_ok;
    bool notify = app->controller_sync_notify;
    char body[sizeof(app->controller_sync_body)];
    snprintf(body, sizeof(body), "%s", app->controller_sync_body);

    ha_remote_join_controller_sync(app);

    bool parsed = ok && (HA_REMOTE_ACTION_COUNT == 0 || ha_remote_parse_states(app, body));
    bool show_status =
        app->current_view == HA_REMOTE_VIEW_CONTROLLER &&
        !ha_remote_command_queue_active(app);

    if(parsed) {
        if(HA_REMOTE_ACTION_COUNT == 0) {
            ha_remote_update_labels(app);
        }
        if(show_status) {
            ha_remote_set_header(app, "States updated", true);
        }
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Controller entry sync OK: %s", body);
        }
        if(notify) {
            ha_remote_notify(app, &sequence_single_vibro);
        }
        return;
    }

    if(show_status) {
        ha_remote_set_header(app, body[0] ? body : "Refresh failed", true);
    }
    if(ha_remote_log_error_enabled(app)) {
        FURI_LOG_W(
            HA_REMOTE_LOG_TAG, "Controller entry sync failed: %s", body[0] ? body : "unknown error");
    }
    if(notify) {
        ha_remote_notify(app, &sequence_error);
    }
}

#if HA_REMOTE_ACTION_COUNT > 0
static char ha_remote_upper_hex(char c) {
    if(c >= 'a' && c <= 'f') {
        return c - 32;
    }
    return c;
}
#endif

static int32_t ha_remote_action_index_from_id(char high, char low) {
#if HA_REMOTE_ACTION_COUNT > 0
    char id[3] = {ha_remote_upper_hex(high), ha_remote_upper_hex(low), '\0'};

    for(size_t i = 0; i < HA_REMOTE_ACTION_COUNT; i++) {
        char expected[3];
        snprintf(expected, sizeof(expected), "%02X", ha_remote_actions[i].action_id);
        if(strcmp(id, expected) == 0) {
            return i;
        }
    }
#else
    UNUSED(high);
    UNUSED(low);
#endif

    return -1;
}

static bool ha_remote_parse_states(HaRemoteApp* app, const char* body) {
    bool parsed_any = false;
    const char* p = body;

    while(*p) {
        while(*p == ' ' || *p == '|') {
            p++;
        }

        if(!p[0] || !p[1]) {
            break;
        }

        int32_t action_index = ha_remote_action_index_from_id(p[0], p[1]);
        p += 2;

        while(*p == ' ') {
            p++;
        }
        if(*p != '=') {
            while(*p && *p != '|') {
                p++;
            }
            continue;
        }
        p++;
        while(*p == ' ') {
            p++;
        }

        const char* state_start = p;
        while(*p && *p != '|') {
            p++;
        }

        if(action_index >= 0) {
            ha_remote_copy_trimmed(
                app->action_states[action_index],
                sizeof(app->action_states[action_index]),
                state_start,
                (size_t)(p - state_start));
            app->action_state_known[action_index] = true;
            parsed_any = true;
        }
    }

    if(parsed_any) {
        ha_remote_update_labels(app);
    }

    return parsed_any;
}

static bool ha_remote_key_equals(const char* key_start, size_t key_len, const char* expected) {
    return strlen(expected) == key_len && strncmp(key_start, expected, key_len) == 0;
}

static bool ha_remote_value_equals(const char* value, const char* expected) {
    return strcmp(value, expected) == 0;
}

static uint8_t ha_remote_thermo_all_mask(uint8_t count) {
    return count >= 8 ? 0xFF : (uint8_t)((1U << count) - 1U);
}

static bool ha_remote_thermo_index_allowed(uint8_t mask, uint8_t index) {
    return mask == 0 || (mask & (1U << index));
}

static uint8_t ha_remote_thermo_first_allowed(uint8_t mask, uint8_t count) {
    if(mask == 0) {
        return 0;
    }
    for(uint8_t i = 0; i < count; i++) {
        if(mask & (1U << i)) {
            return i;
        }
    }
    return 0;
}

static uint8_t ha_remote_thermo_cycle_index(uint8_t index, int8_t delta, uint8_t mask, uint8_t count) {
    if(mask == 0 || count == 0) {
        return index;
    }

    int8_t candidate = (int8_t)index;
    for(uint8_t i = 0; i < count; i++) {
        candidate += delta;
        if(candidate < 0) {
            candidate = (int8_t)count - 1;
        } else if(candidate >= (int8_t)count) {
            candidate = 0;
        }
        if(mask & (1U << candidate)) {
            return (uint8_t)candidate;
        }
    }
    return index;
}

static uint8_t ha_remote_thermo_mask_from_list(
    const HaRemoteThermoOption* options,
    uint8_t count,
    const char* start,
    size_t len) {
    uint8_t mask = 0;
    const char* cursor = start;
    const char* end = start + len;

    while(cursor < end) {
        const char* token_start = cursor;
        while(cursor < end && *cursor != ',') {
            cursor++;
        }
        char value[HA_REMOTE_THERMO_VALUE_MAX];
        ha_remote_copy_trimmed(value, sizeof(value), token_start, (size_t)(cursor - token_start));
        for(uint8_t i = 0; i < count; i++) {
            if(ha_remote_value_equals(value, options[i].value)) {
                mask |= (1U << i);
                break;
            }
        }
        if(cursor < end && *cursor == ',') {
            cursor++;
        }
    }

    return mask;
}

static uint8_t ha_remote_thermo_mode_index(const char* value) {
    for(uint8_t i = 0; i < HA_REMOTE_THERMO_MODE_COUNT; i++) {
        if(ha_remote_value_equals(value, ha_remote_thermo_modes[i].value)) {
            return i;
        }
    }
    return 0;
}

static uint8_t ha_remote_thermo_fan_index(const char* value) {
    for(uint8_t i = 0; i < HA_REMOTE_THERMO_FAN_COUNT; i++) {
        if(ha_remote_value_equals(value, ha_remote_thermo_fans[i].value)) {
            return i;
        }
    }
    return 0;
}

static bool ha_remote_thermostat_target_dirty(HaRemoteThermostatModel* model) {
    return model->known && model->draft_target != model->actual_target;
}

static bool ha_remote_thermostat_mode_dirty(HaRemoteThermostatModel* model) {
    return model->known && model->draft_mode != model->actual_mode;
}

static bool ha_remote_thermostat_fan_dirty(HaRemoteThermostatModel* model) {
    return model->known && model->fan_supported && model->draft_fan != model->actual_fan;
}

static void ha_remote_thermostat_recompute_dirty(HaRemoteThermostatModel* model) {
    model->dirty = ha_remote_thermostat_target_dirty(model) ||
                   ha_remote_thermostat_mode_dirty(model) ||
                   ha_remote_thermostat_fan_dirty(model);
}

static void ha_remote_title_case(char* out, size_t out_size, const char* value) {
    if(!value || !value[0] || ha_remote_value_equals(value, "--")) {
        snprintf(out, out_size, "--");
        return;
    }

    snprintf(out, out_size, "%s", value);
    if(out[0] >= 'a' && out[0] <= 'z') {
        out[0] -= 32;
    }
}

static bool ha_remote_state_is_off(const char* value) {
    return value && (ha_remote_value_equals(value, "off") || ha_remote_value_equals(value, "OFF"));
}

static void ha_remote_init_thermostat_state(HaRemoteApp* app) {
    snprintf(app->thermostat_temp, sizeof(app->thermostat_temp), "--");
    snprintf(app->thermostat_target, sizeof(app->thermostat_target), "--");
    snprintf(app->thermostat_mode, sizeof(app->thermostat_mode), "--");
    snprintf(app->thermostat_action, sizeof(app->thermostat_action), "--");
    snprintf(app->thermostat_fan, sizeof(app->thermostat_fan), "--");
    snprintf(app->thermostat_humidity, sizeof(app->thermostat_humidity), "--");
    app->thermostat_mode_mask = ha_remote_thermo_all_mask(HA_REMOTE_THERMO_MODE_COUNT);
    app->thermostat_fan_mask = 0;
    app->thermostat_state_known = false;
}

static bool ha_remote_parse_thermostat(HaRemoteApp* app, const char* body) {
    bool parsed_any = false;
    const char* p = body;

    while(*p) {
        while(*p == ' ' || *p == '|') {
            p++;
        }

        const char* key_start = p;
        while(*p && *p != '=' && *p != '|') {
            p++;
        }

        if(*p != '=') {
            while(*p && *p != '|') {
                p++;
            }
            continue;
        }

        size_t key_len = (size_t)(p - key_start);
        p++;
        while(*p == ' ') {
            p++;
        }

        const char* value_start = p;
        while(*p && *p != '|') {
            p++;
        }
        size_t value_len = (size_t)(p - value_start);

        if(ha_remote_key_equals(key_start, key_len, "temp")) {
            ha_remote_copy_trimmed(
                app->thermostat_temp, sizeof(app->thermostat_temp), value_start, value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "target")) {
            ha_remote_copy_trimmed(
                app->thermostat_target, sizeof(app->thermostat_target), value_start, value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "mode")) {
            ha_remote_copy_trimmed(
                app->thermostat_mode, sizeof(app->thermostat_mode), value_start, value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "action")) {
            ha_remote_copy_trimmed(
                app->thermostat_action, sizeof(app->thermostat_action), value_start, value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "fan")) {
            ha_remote_copy_trimmed(
                app->thermostat_fan, sizeof(app->thermostat_fan), value_start, value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "hum")) {
            ha_remote_copy_trimmed(
                app->thermostat_humidity,
                sizeof(app->thermostat_humidity),
                value_start,
                value_len);
            parsed_any = true;
        } else if(ha_remote_key_equals(key_start, key_len, "modes")) {
            uint8_t mask = ha_remote_thermo_mask_from_list(
                ha_remote_thermo_modes, HA_REMOTE_THERMO_MODE_COUNT, value_start, value_len);
            if(mask) {
                app->thermostat_mode_mask = mask;
            }
        } else if(ha_remote_key_equals(key_start, key_len, "fans")) {
            app->thermostat_fan_mask = ha_remote_thermo_mask_from_list(
                ha_remote_thermo_fans, HA_REMOTE_THERMO_FAN_COUNT, value_start, value_len);
        }
    }

    if(parsed_any) {
        app->thermostat_state_known = true;
        ha_remote_sync_thermostat_model(app, false);
    }

    return parsed_any;
}

static void ha_remote_update_labels(HaRemoteApp* app) {
    ha_remote_controller_model_sync(app, true);
}

static void ha_remote_thermostat_model_init(HaRemoteApp* app) {
    if(!app->thermostat_view) {
        return;
    }

    HaRemoteThermostatModel* model = view_get_model(app->thermostat_view);
    snprintf(model->status, sizeof(model->status), "Thermostat");
    snprintf(model->actual, sizeof(model->actual), "--");
    snprintf(model->humidity, sizeof(model->humidity), "--");
    snprintf(model->current_target, sizeof(model->current_target), "--");
    snprintf(model->action, sizeof(model->action), "--");
    model->actual_target = 72;
    model->draft_target = 72;
    model->mode_mask = ha_remote_thermo_all_mask(HA_REMOTE_THERMO_MODE_COUNT);
    model->fan_mask = 0;
    model->actual_mode = ha_remote_thermo_mode_index("cool");
    model->draft_mode = ha_remote_thermo_mode_index("cool");
    model->edit_mode_start = model->draft_mode;
    model->actual_fan = 0;
    model->draft_fan = 0;
    model->edit_fan_start = model->draft_fan;
    model->focus = HaRemoteThermoFocusSetpoint;
    model->configured = app->thermostat_configured;
    model->known = false;
    model->fan_supported = false;
    model->editing = false;
    model->dirty = false;
    view_commit_model(app->thermostat_view, false);
}

static void ha_remote_thermostat_set_status(HaRemoteApp* app, const char* status) {
    if(!app->thermostat_view) {
        return;
    }

    if(!status) {
        status = "";
    }

    HaRemoteThermostatModel* model = view_get_model(app->thermostat_view);
    snprintf(model->status, sizeof(model->status), "%s", status);
    view_commit_model(app->thermostat_view, true);
}

static void ha_remote_sync_thermostat_model(HaRemoteApp* app, bool preserve_dirty) {
    if(!app->thermostat_view) {
        return;
    }

    HaRemoteThermostatModel* model = view_get_model(app->thermostat_view);
    snprintf(model->actual, sizeof(model->actual), "%s", app->thermostat_temp);
    snprintf(model->humidity, sizeof(model->humidity), "%s", app->thermostat_humidity);
    snprintf(model->current_target, sizeof(model->current_target), "%s", app->thermostat_target);
    snprintf(model->action, sizeof(model->action), "%s", app->thermostat_action);
    model->configured = app->thermostat_configured;
    model->known = app->thermostat_state_known;
    model->mode_mask = app->thermostat_mode_mask ?
                           app->thermostat_mode_mask :
                           ha_remote_thermo_all_mask(HA_REMOTE_THERMO_MODE_COUNT);
    model->fan_mask = app->thermostat_fan_mask;
    model->fan_supported = model->fan_mask != 0;

    int target = atoi(app->thermostat_target);
    model->actual_target = target > 0 ? target : 72;
    model->actual_mode = ha_remote_thermo_mode_index(app->thermostat_mode);
    if(!ha_remote_thermo_index_allowed(model->mode_mask, model->actual_mode)) {
        model->actual_mode = ha_remote_thermo_first_allowed(model->mode_mask, HA_REMOTE_THERMO_MODE_COUNT);
    }
    model->actual_fan = model->fan_supported ? ha_remote_thermo_fan_index(app->thermostat_fan) : 0;
    if(model->fan_supported && !ha_remote_thermo_index_allowed(model->fan_mask, model->actual_fan)) {
        model->actual_fan = ha_remote_thermo_first_allowed(model->fan_mask, HA_REMOTE_THERMO_FAN_COUNT);
    }

    if(!preserve_dirty || !model->dirty) {
        model->draft_target = model->actual_target;
        model->draft_mode = model->actual_mode;
        model->draft_fan = model->actual_fan;
        model->edit_mode_start = model->draft_mode;
        model->edit_fan_start = model->draft_fan;
        model->editing = false;
    }

    ha_remote_thermostat_recompute_dirty(model);

    view_commit_model(app->thermostat_view, true);
}

static uint8_t ha_remote_clamp_dimmer_value(int16_t value) {
    if(value < 0) {
        return 0;
    }
    if(value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

static uint8_t ha_remote_dimmer_value_from_state(HaRemoteApp* app, uint8_t entry_id) {
    bool known = false;
    const char* state = ha_remote_entry_state(app, entry_id, &known);
    if(!known) {
        return 50;
    }

    if(ha_remote_state_is_off(state)) {
        return 0;
    }

    int value = atoi(state);
    if(value <= 0 && !ha_remote_value_equals(state, "0") && !ha_remote_value_equals(state, "0%")) {
        return 50;
    }
    return ha_remote_clamp_dimmer_value(value);
}

static void ha_remote_dimmer_reset_repeat(HaRemoteDimmerModel* model) {
    model->repeat_count = 0;
    model->repeat_key = InputKeyMAX;
}

static uint8_t ha_remote_dimmer_step_for_event(HaRemoteDimmerModel* model, InputEvent* event) {
    if(event->type == InputTypeShort || model->repeat_key != event->key) {
        model->repeat_key = event->key;
        model->repeat_count = 0;
        return HA_REMOTE_DIMMER_TAP_STEP;
    }

    if(model->repeat_count < 250) {
        model->repeat_count++;
    }

    if(model->repeat_count >= HA_REMOTE_DIMMER_REPEAT_TURBO_AFTER) {
        return 10;
    }
    if(model->repeat_count >= HA_REMOTE_DIMMER_REPEAT_FAST_AFTER) {
        return 5;
    }
    if(model->repeat_count >= HA_REMOTE_DIMMER_REPEAT_MED_AFTER) {
        return 2;
    }
    return HA_REMOTE_DIMMER_TAP_STEP;
}

static void ha_remote_dimmer_model_sync(HaRemoteApp* app, uint8_t entry_id) {
    if(!app->dimmer_view || !ha_remote_entry_is_dimmer(app, entry_id)) {
        return;
    }

    bool known = false;
    (void)ha_remote_entry_state(app, entry_id, &known);
    uint8_t value = ha_remote_dimmer_value_from_state(app, entry_id);
    HaRemoteDimmerModel* model = view_get_model(app->dimmer_view);
    snprintf(model->title, sizeof(model->title), "%s", ha_remote_entry_label(app, entry_id));
    model->entry_id = entry_id;
    model->value = value;
    model->original = value;
    model->known = known;
    ha_remote_dimmer_reset_repeat(model);
    view_commit_model(app->dimmer_view, false);
}

static void ha_remote_open_dimmer(HaRemoteApp* app, uint8_t entry_id) {
    if(!ha_remote_enqueue_pending_command(app)) {
        return;
    }
    ha_remote_dimmer_model_sync(app, entry_id);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_DIMMER);
}

static void ha_remote_dimmer_draw_callback(Canvas* canvas, void* model_context) {
    HaRemoteDimmerModel* model = model_context;
    char value[8];
    char original[8];
    uint8_t fill = (uint8_t)((model->value * 76) / 100);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 8, 8, 112, 48);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 20, AlignCenter, AlignBottom, model->title);
    canvas_draw_str(canvas, 18, 38, "<");
    canvas_draw_str(canvas, 105, 38, ">");

    canvas_set_font(canvas, FontPrimary);
    snprintf(value, sizeof(value), "%u%%", (unsigned)model->value);
    canvas_draw_str_aligned(canvas, 64, 39, AlignCenter, AlignBottom, value);

    canvas_draw_frame(canvas, 25, 43, 78, 7);
    if(fill > 0) {
        canvas_draw_box(canvas, 26, 44, fill, 5);
    }

    canvas_set_font(canvas, FontSecondary);
    if(model->known) {
        snprintf(original, sizeof(original), "%u%%", (unsigned)model->original);
    } else {
        snprintf(original, sizeof(original), "--");
    }
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, original);
}

static bool ha_remote_dimmer_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    HaRemoteDimmerModel* model = view_get_model(app->dimmer_view);

    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        uint8_t step = ha_remote_dimmer_step_for_event(model, event);
        int16_t delta = event->key == InputKeyRight ? step : -(int16_t)step;
        model->value = ha_remote_clamp_dimmer_value((int16_t)model->value + delta);
        view_commit_model(app->dimmer_view, true);
        return true;
    }

    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        ha_remote_dimmer_reset_repeat(model);
        view_commit_model(app->dimmer_view, false);
        return true;
    }

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        ha_remote_dimmer_reset_repeat(model);
        view_commit_model(app->dimmer_view, false);
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_controller_model_sync(app, true);
        return true;
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        uint8_t entry_id = model->entry_id;
        uint8_t value = model->value;
        ha_remote_dimmer_reset_repeat(model);
        view_commit_model(app->dimmer_view, false);
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_schedule_dimmer_command(app, entry_id, value);
        return true;
    }

    view_commit_model(app->dimmer_view, false);
    return false;
}

static void ha_remote_draw_house_icon(Canvas* canvas, int32_t x, int32_t y) {
    canvas_draw_icon(canvas, x, y, &I_flipperha_10x10);
}

static void ha_remote_draw_fit_str(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    const char* text) {
    char fit[HA_REMOTE_LABEL_MAX];
    snprintf(fit, sizeof(fit), "%s", text ? text : "");

    if(canvas_string_width(canvas, fit) > width) {
        size_t len = strlen(fit);
        while(len > 2 && canvas_string_width(canvas, fit) > width) {
            fit[--len] = '\0';
        }
        if(len > 2) {
            fit[len - 1] = '.';
            fit[len - 2] = '.';
        }
    }

    canvas_draw_str(canvas, x, y, fit);
}

// Draw a horizontal window of `text` starting at `offset` chars, clipped to
// `width` (no ellipsis). Sliding `offset` scrolls the text like a chyron.
static void ha_remote_draw_marquee_str(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    const char* text,
    uint16_t offset) {
    if(!text) {
        return;
    }
    size_t len = strlen(text);
    if(offset > len) {
        offset = (uint16_t)len;
    }
    char fit[HA_REMOTE_ENTITY_ID_MAX + 1];
    snprintf(fit, sizeof(fit), "%s", text + offset);
    size_t flen = strlen(fit);
    while(flen > 0 && canvas_string_width(canvas, fit) > width) {
        fit[--flen] = '\0';
    }
    canvas_draw_str(canvas, x, y, fit);
}

static void ha_remote_controller_draw_refresh_icon(Canvas* canvas, int32_t x, int32_t y) {
    static const uint16_t rows[] = {
        0x07D,
        0x083,
        0x107,
        0x100,
        0x100,
        0x101,
        0x101,
        0x082,
        0x07C,
    };

    for(uint8_t row = 0; row < COUNT_OF(rows); row++) {
        for(uint8_t col = 0; col < 9; col++) {
            if(rows[row] & (1 << (8 - col))) {
                canvas_draw_dot(canvas, x + col, y + row);
            }
        }
    }
}

static const char* ha_remote_controller_tool_label(uint8_t tool) {
    if(tool == HA_REMOTE_CONTROLLER_TOOL_INFO) {
        return "Info";
    }
    if(tool == HA_REMOTE_CONTROLLER_TOOL_REORDER) {
        return "Reorder";
    }
    return "Add";
}

static void ha_remote_controller_draw_delete_button(Canvas* canvas, int32_t y, bool selected) {
    int32_t x = HA_REMOTE_CONTROLLER_DELETE_BOX_X;
    int32_t size = HA_REMOTE_CONTROLLER_DELETE_BOX_SIZE;

    if(selected) {
        canvas_draw_box(canvas, x, y + 1, size, size);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y + 1, size, size);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, x, y + 1, size, size);
    }

    canvas_draw_line(canvas, x + 3, y + 4, x + 6, y + 7);
    canvas_draw_line(canvas, x + 6, y + 4, x + 3, y + 7);

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void ha_remote_controller_draw_tool_button(
    Canvas* canvas,
    uint8_t tool,
    bool selected,
    bool active) {
    int32_t x = HA_REMOTE_CONTROLLER_RAIL_X;
    int32_t y = HA_REMOTE_CONTROLLER_RAIL_Y +
                (tool * (HA_REMOTE_CONTROLLER_RAIL_BUTTON_SIZE +
                         HA_REMOTE_CONTROLLER_RAIL_BUTTON_GAP));
    int32_t size = HA_REMOTE_CONTROLLER_RAIL_BUTTON_SIZE;

    if(selected) {
        canvas_draw_box(canvas, x, y, size, size);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, x, y, size, size);
        if(active) {
            canvas_draw_frame(canvas, x + 2, y + 2, size - 4, size - 4);
        }
    }

    if(tool == HA_REMOTE_CONTROLLER_TOOL_INFO) {
        canvas_draw_box(canvas, x + 6, y + 3, 1, 1);
        canvas_draw_line(canvas, x + 6, y + 5, x + 6, y + 9);
        canvas_draw_line(canvas, x + 5, y + 9, x + 7, y + 9);
    } else if(tool == HA_REMOTE_CONTROLLER_TOOL_REORDER) {
        canvas_draw_line(canvas, x + 3, y + 3, x + 9, y + 3);
        canvas_draw_line(canvas, x + 3, y + 6, x + 9, y + 6);
        canvas_draw_line(canvas, x + 3, y + 9, x + 9, y + 9);
    } else {
        canvas_draw_line(canvas, x + 6, y + 3, x + 6, y + 9);
        canvas_draw_line(canvas, x + 3, y + 6, x + 9, y + 6);
    }

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static uint8_t ha_remote_controller_queue_total(HaRemoteApp* app) {
    uint32_t total = app->command_queue ? furi_message_queue_get_count(app->command_queue) : 0;
    if(app->command_worker_busy) {
        total++;
    }

    if(total == 0 && app->pending_command_type != HaRemotePendingCommandNone) {
        total++;
    }
    if(total > HA_REMOTE_COMMAND_QUEUE_CAPACITY) {
        return HA_REMOTE_COMMAND_QUEUE_CAPACITY;
    }
    return (uint8_t)total;
}

static void ha_remote_bridge_host(HaRemoteApp* app, char* out, size_t out_size) {
    if(!app->bridge_configured) {
        snprintf(out, out_size, "setup needed");
        return;
    }

    const char* start = strstr(app->bridge_base_url, "://");
    start = start ? start + 3 : app->bridge_base_url;

    size_t len = strcspn(start, "/?#");
    if(len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';
}

static void ha_remote_controller_info_model_sync(HaRemoteApp* app, bool update) {
    if(!app->controller_info_view) {
        return;
    }

    uint8_t known_states = 0;
#if HA_REMOTE_ACTION_COUNT > 0
    for(uint8_t i = 0; i < HA_REMOTE_ACTION_COUNT; i++) {
        if(app->action_state_known[i]) {
            known_states++;
        }
    }
#endif
    for(uint8_t i = 0; i < app->custom_entry_count; i++) {
        if(app->custom_state_known[i]) {
            known_states++;
        }
    }

    const char* sync_state = "No data";
    if(!app->bridge_configured) {
        sync_state = "Setup needed";
    } else if(app->controller_sync_running) {
        sync_state = "Syncing";
    } else if(app->controller_sync_ok) {
        sync_state = "OK";
    } else if(app->controller_sync_body[0]) {
        sync_state = "Failed";
    } else if(known_states > 0) {
        sync_state = "Cached";
    }

    char host[HA_REMOTE_INFO_LINE_MAX];
    ha_remote_bridge_host(app, host, sizeof(host));

    HaRemoteControllerInfoModel* model = view_get_model(app->controller_info_view);
    snprintf(model->title, sizeof(model->title), "HA Link");
    snprintf(model->lines[0], sizeof(model->lines[0]), "Sync: %s", sync_state);
    snprintf(model->lines[1], sizeof(model->lines[1]), "HA: %.35s", host);
    snprintf(
        model->lines[2],
        sizeof(model->lines[2]),
        "UART: %s %lu",
        app->uart_handle ? "open" : "idle",
        (unsigned long)HA_REMOTE_UART_BAUD);
    snprintf(
        model->lines[3],
        sizeof(model->lines[3]),
        "States: %u/%u Log:%s",
        (unsigned)known_states,
        (unsigned)ha_remote_controller_total_entry_count(app),
        ha_remote_log_mode_labels[app->log_mode]);
    snprintf(
        model->lines[4],
        sizeof(model->lines[4]),
        "Cfg:%s BLE:%s",
        app->bridge_configured ? (app->bridge_config_from_file ? "file" : "build") : "setup",
        HA_REMOTE_ACTION_COUNT > 0 ? "Long OK" : "Off");
    view_commit_model(app->controller_info_view, update);
}

static const char* ha_remote_controller_item_label(HaRemoteApp* app, uint8_t item_index) {
    if(item_index == HA_REMOTE_REFRESH_INDEX) {
        return "Refresh States";
    }

    uint8_t action_index;
    if(ha_remote_controller_action_index_for_item(app, item_index, &action_index)) {
        return ha_remote_actions[action_index].label;
    }

    uint8_t entry_id;
    if(ha_remote_controller_entry_id_for_item(app, item_index, &entry_id)) {
        return ha_remote_entry_label(app, entry_id);
    }

    return "Controller";
}

static const char*
    ha_remote_command_label(HaRemoteApp* app, HaRemotePendingCommandType type, uint8_t entry_id) {
    if((type == HaRemotePendingCommandAction || type == HaRemotePendingCommandDimmer) &&
       ha_remote_entry_id_is_valid(app, entry_id)) {
        return ha_remote_entry_label(app, entry_id);
    }

    if(type == HaRemotePendingCommandThermostat) {
        return "Thermostat";
    }

    return "Command";
}

static bool ha_remote_status_header_is_alert(const char* header) {
    return header && header[0] &&
           (strstr(header, "Failed") || strstr(header, "failed") || strstr(header, "Full") ||
            strstr(header, "Canceled") || strstr(header, "busy") || strstr(header, "timeout") ||
            strstr(header, "no board"));
}

static void ha_remote_controller_title(HaRemoteApp* app, char* out, size_t out_size) {
    if(app->controller_reorder_delete_confirm) {
        snprintf(out, out_size, "Delete Row");
        return;
    }

    if(app->status_clear_enabled && ha_remote_status_header_is_alert(app->status_header)) {
        snprintf(out, out_size, "%s", app->status_header);
        return;
    }

    if(ha_remote_controller_queue_total(app) > 0) {
        if(app->command_worker_busy) {
            snprintf(
                out,
                out_size,
                "Sending %.20s",
                ha_remote_command_label(
                    app, app->command_worker_type, app->command_worker_action_index));
        } else if(app->pending_command_type != HaRemotePendingCommandNone) {
            snprintf(
                out,
                out_size,
                "Pending %.20s",
                ha_remote_command_label(app, app->pending_command_type, app->pending_action_index));
        } else {
            snprintf(out, out_size, "Queued");
        }
        return;
    }

    if(app->controller_tool_focus) {
        snprintf(out, out_size, "%s", ha_remote_controller_tool_label(app->controller_tool_selected));
        return;
    }

    if(app->controller_reorder_mode) {
        if(app->controller_reorder_grabbed) {
            snprintf(
                out,
                out_size,
                "Move %.22s",
                ha_remote_controller_item_label(app, app->controller_selected));
        } else if(app->controller_reorder_delete_focus) {
            snprintf(
                out,
                out_size,
                "Delete %.21s",
                ha_remote_controller_item_label(app, app->controller_selected));
        } else {
            snprintf(out, out_size, "Reorder");
        }
        return;
    }

    if(app->controller_sync_running) {
        snprintf(out, out_size, "Syncing states");
        return;
    }

    if(app->status_header[0] && !ha_remote_value_equals(app->status_header, "Controller")) {
        snprintf(out, out_size, "%s", app->status_header);
        return;
    }

    if(app->action_order_count == 0) {
        snprintf(out, out_size, "Controller");
        return;
    }

    snprintf(
        out,
        out_size,
        "%s",
        ha_remote_controller_item_label(app, app->controller_selected));
}

static void ha_remote_controller_model_sync(HaRemoteApp* app, bool update) {
    if(!app->controller_view) {
        return;
    }

    uint8_t item_count = ha_remote_controller_item_count(app);
    if(item_count == 0) {
        item_count = 1;
    }

    if(app->controller_selected >= item_count) {
        app->controller_selected = 0;
    }
    if(app->controller_selected == HA_REMOTE_REFRESH_INDEX) {
        app->controller_reorder_delete_focus = false;
    }

    if(app->controller_selected < app->controller_scroll) {
        app->controller_scroll = app->controller_selected;
    } else if(
        app->controller_selected >= app->controller_scroll + HA_REMOTE_CONTROLLER_VISIBLE_ROWS) {
        app->controller_scroll = app->controller_selected - HA_REMOTE_CONTROLLER_VISIBLE_ROWS + 1;
    }

    uint8_t max_scroll = item_count > HA_REMOTE_CONTROLLER_VISIBLE_ROWS ?
                             item_count - HA_REMOTE_CONTROLLER_VISIBLE_ROWS :
                             0;
    if(app->controller_scroll > max_scroll) {
        app->controller_scroll = max_scroll;
    }

    HaRemoteControllerModel* model = view_get_model(app->controller_view);
    ha_remote_controller_title(app, model->title, sizeof(model->title));
    model->selected = app->controller_selected;
    model->scroll = app->controller_scroll;
    model->tool_selected = app->controller_tool_selected;
    model->action_order_count = app->action_order_count;
    model->item_count = item_count;
    model->syncing = app->controller_sync_running;
    model->tool_focus = app->controller_tool_focus;
    model->reorder_mode = app->controller_reorder_mode;
    model->reorder_grabbed = app->controller_reorder_grabbed;
    model->reorder_delete_focus = app->controller_reorder_delete_focus;
    model->reorder_delete_confirm = app->controller_reorder_delete_confirm;
    memcpy(model->action_order, app->action_order, sizeof(model->action_order));

    uint8_t delete_entry_id;
    if(ha_remote_controller_entry_id_for_item(app, app->controller_selected, &delete_entry_id)) {
        snprintf(
            model->delete_label,
            sizeof(model->delete_label),
            "%s",
            ha_remote_entry_label(app, delete_entry_id));
    } else {
        snprintf(model->delete_label, sizeof(model->delete_label), "row");
    }

    uint8_t queue_total = ha_remote_controller_queue_total(app);
    model->queue_visible = queue_total > 0;
    if(model->queue_visible) {
        snprintf(model->queue, sizeof(model->queue), "1/%u", (unsigned)queue_total);
    } else {
        model->queue[0] = '\0';
    }

    uint8_t total_entries = ha_remote_controller_total_entry_count(app);
    for(uint8_t i = 0; i < total_entries; i++) {
        bool known = false;
        const char* state = ha_remote_entry_state(app, i, &known);
        snprintf(model->labels[i], sizeof(model->labels[i]), "%s", ha_remote_entry_label(app, i));
        if(!ha_remote_entry_has_state(app, i)) {
            snprintf(model->states[i], sizeof(model->states[i]), "Run");
            model->state_known[i] = true;
        } else if(known) {
            ha_remote_format_entry_state(app, i, state, model->states[i], sizeof(model->states[i]));
            model->state_known[i] = true;
        } else {
            snprintf(model->states[i], sizeof(model->states[i]), "--");
            model->state_known[i] = false;
        }
    }
    for(uint8_t i = total_entries; i < HA_REMOTE_CONTROLLER_ENTRY_MAX; i++) {
        snprintf(model->labels[i], sizeof(model->labels[i]), "Controller");
        snprintf(model->states[i], sizeof(model->states[i]), "--");
        model->state_known[i] = false;
    }

    view_commit_model(app->controller_view, update);
}

static void ha_remote_controller_draw_callback(Canvas* canvas, void* model_context) {
    HaRemoteControllerModel* model = model_context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    ha_remote_draw_house_icon(canvas, 2, 0);
    size_t title_width = model->queue_visible ? 83 : 110;
    ha_remote_draw_fit_str(canvas, 15, 9, title_width, model->title);
    if(model->queue_visible) {
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, model->queue);
    }
    canvas_draw_line(canvas, 0, 11, 124, 11);

    ha_remote_controller_draw_tool_button(
        canvas,
        HA_REMOTE_CONTROLLER_TOOL_INFO,
        model->tool_focus && model->tool_selected == HA_REMOTE_CONTROLLER_TOOL_INFO,
        false);
    ha_remote_controller_draw_tool_button(
        canvas,
        HA_REMOTE_CONTROLLER_TOOL_REORDER,
        model->tool_focus && model->tool_selected == HA_REMOTE_CONTROLLER_TOOL_REORDER,
        model->reorder_mode);
    ha_remote_controller_draw_tool_button(
        canvas,
        HA_REMOTE_CONTROLLER_TOOL_PLUS,
        model->tool_focus && model->tool_selected == HA_REMOTE_CONTROLLER_TOOL_PLUS,
        false);

    if(model->reorder_delete_confirm) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 8, 18, 112, 36);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 8, 18, 112, 36);
        canvas_draw_str(canvas, 14, 29, "Delete row?");
        ha_remote_draw_fit_str(canvas, 14, 42, 98, model->delete_label);
        canvas_draw_str_aligned(canvas, 114, 57, AlignRight, AlignBottom, "OK Delete");
        return;
    }

    for(uint8_t row = 0; row < HA_REMOTE_CONTROLLER_VISIBLE_ROWS; row++) {
        uint8_t item_index = model->scroll + row;
        if(item_index >= model->item_count) {
            break;
        }

        int32_t y = 12 + (row * 13);
        bool selected = !model->tool_focus && model->selected == item_index;
        bool delete_visible =
            model->reorder_mode && !model->reorder_grabbed && item_index > HA_REMOTE_REFRESH_INDEX;
        bool delete_selected = selected && model->reorder_delete_focus && delete_visible;
        bool row_selected = selected && !delete_selected;
        const char* label = "Refresh";
        const char* state = "";

        if(item_index > HA_REMOTE_REFRESH_INDEX) {
            uint8_t order_slot = item_index - HA_REMOTE_ACTION_MENU_BASE;
            uint8_t entry_id =
                order_slot < model->action_order_count ? model->action_order[order_slot] : order_slot;
            if(entry_id < HA_REMOTE_CONTROLLER_ENTRY_MAX) {
                label = model->labels[entry_id];
                state = model->states[entry_id];
            }
        } else if(model->syncing) {
            state = "...";
        }

        bool reorder_row = model->reorder_mode && item_index > HA_REMOTE_REFRESH_INDEX;
        bool refresh_row = item_index == HA_REMOTE_REFRESH_INDEX;
        bool draw_state = state[0] && !refresh_row;
        int32_t row_left =
            model->reorder_mode ? HA_REMOTE_CONTROLLER_HANDLE_X : HA_REMOTE_CONTROLLER_LIST_X;

        if(row_selected) {
            uint8_t row_width = HA_REMOTE_CONTROLLER_ROW_RIGHT - row_left;
            canvas_draw_box(canvas, row_left, y, row_width, 12);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_set_font(canvas, FontSecondary);
        if(reorder_row) {
            canvas_draw_line(
                canvas,
                HA_REMOTE_CONTROLLER_HANDLE_X,
                y + 4,
                HA_REMOTE_CONTROLLER_HANDLE_X + 2,
                y + 4);
            canvas_draw_line(
                canvas,
                HA_REMOTE_CONTROLLER_HANDLE_X,
                y + 7,
                HA_REMOTE_CONTROLLER_HANDLE_X + 2,
                y + 7);
        }
        if(draw_state) {
            if(!delete_visible) {
                canvas_draw_str_aligned(
                    canvas, HA_REMOTE_CONTROLLER_STATE_X, y + 10, AlignRight, AlignBottom, state);
            }
        }
        ha_remote_draw_fit_str(
            canvas,
            HA_REMOTE_CONTROLLER_LABEL_X,
            y + 10,
            delete_visible ? 83 : (draw_state ? 66 : 88),
            label);
        if(refresh_row) {
            ha_remote_controller_draw_refresh_icon(canvas, HA_REMOTE_CONTROLLER_STATE_X - 11, y + 2);
        }
        if(delete_visible) {
            if(row_selected) {
                canvas_set_color(canvas, ColorBlack);
            }
            ha_remote_controller_draw_delete_button(canvas, y, delete_selected);
            if(row_selected) {
                canvas_set_color(canvas, ColorWhite);
            }
        }

        if(row_selected) {
            canvas_set_color(canvas, ColorBlack);
        }
        canvas_draw_line(
            canvas,
            row_left,
            y + 12,
            HA_REMOTE_CONTROLLER_ROW_RIGHT - 3,
            y + 12);
    }

    if(model->action_order_count == 0 && !model->reorder_mode && !model->reorder_delete_confirm) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, HA_REMOTE_CONTROLLER_LABEL_X, 36, "No rows yet");
        canvas_draw_str(canvas, HA_REMOTE_CONTROLLER_LABEL_X, 48, "Add devices");
    }

    if(model->item_count > HA_REMOTE_CONTROLLER_VISIBLE_ROWS) {
        elements_scrollbar_pos(
            canvas,
            HA_REMOTE_CONTROLLER_SCROLLBAR_X,
            12,
            52,
            model->selected,
            model->item_count);
    }
}

static void ha_remote_controller_info_draw_callback(Canvas* canvas, void* model_context) {
    HaRemoteControllerInfoModel* model = model_context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    ha_remote_draw_house_icon(canvas, 2, 0);
    ha_remote_draw_fit_str(canvas, 15, 9, 110, model->title);
    canvas_draw_line(canvas, 0, 11, 127, 11);

    canvas_draw_frame(canvas, 3, 15, 122, 47);
    for(uint8_t i = 0; i < HA_REMOTE_INFO_LINE_COUNT; i++) {
        ha_remote_draw_fit_str(canvas, 8, 23 + (i * 9), 112, model->lines[i]);
    }
}

static bool ha_remote_controller_info_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event->type == InputTypeShort &&
       (event->key == InputKeyBack || event->key == InputKeyOk)) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_controller_model_sync(app, true);
        return true;
    }

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        return true;
    }

    return false;
}

static const char* ha_remote_add_category_label(uint8_t category) {
    if(category == HaRemoteAddCategorySwitches) {
        return "Switches";
    }
    if(category == HaRemoteAddCategoryLights) {
        return "Lights";
    }
    if(category == HaRemoteAddCategoryClimate) {
        return "Thermostats";
    }
    return "Routines";
}

static const char* ha_remote_add_category_path(uint8_t category) {
    if(category == HaRemoteAddCategorySwitches) {
        return "switches";
    }
    if(category == HaRemoteAddCategoryLights) {
        return "lights";
    }
    if(category == HaRemoteAddCategoryClimate) {
        return "climate";
    }
    return "routines";
}

static const char* ha_remote_add_capability_label(const char* capability) {
    if(ha_remote_value_equals(capability, "dim")) {
        return "Dimmer";
    }
    if(ha_remote_value_equals(capability, "run")) {
        return "Routine";
    }
    if(ha_remote_value_equals(capability, "thermo")) {
        return "Thermostat";
    }
    return "Toggle";
}

static HaRemoteCatalogItem* ha_remote_add_selected_item(HaRemoteApp* app) {
    if(app->add_selected >= app->add_item_count || app->add_item_count == 0) {
        return NULL;
    }
    return &app->add_items[app->add_selected];
}

static bool ha_remote_parse_uint_token(const char* start, size_t len, uint8_t* value) {
    char buffer[8];
    if(len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
    }
    memcpy(buffer, start, len);
    buffer[len] = '\0';

    int parsed = atoi(buffer);
    if(parsed < 0) {
        parsed = 0;
    } else if(parsed > 255) {
        parsed = 255;
    }
    *value = (uint8_t)parsed;
    return true;
}

static void ha_remote_parse_catalog_field(
    char* out,
    size_t out_size,
    const char* start,
    const char* end) {
    if(end < start) {
        end = start;
    }
    ha_remote_copy_trimmed(out, out_size, start, (size_t)(end - start));
}

static bool ha_remote_parse_catalog_item(
    HaRemoteCatalogItem* item,
    const char* start,
    const char* end) {
    const char* fields[4] = {start, NULL, NULL, NULL};
    const char* cursor = start;
    uint8_t field = 0;

    while(cursor < end && field < 3) {
        if(*cursor == '~') {
            field++;
            fields[field] = cursor + 1;
        }
        cursor++;
    }

    if(!fields[1] || !fields[2] || !fields[3]) {
        return false;
    }

    const char* field_end[4] = {fields[1] - 1, fields[2] - 1, fields[3] - 1, end};
    ha_remote_parse_catalog_field(item->entity_id, sizeof(item->entity_id), fields[0], field_end[0]);
    ha_remote_parse_catalog_field(item->label, sizeof(item->label), fields[1], field_end[1]);
    ha_remote_parse_catalog_field(item->type_label, sizeof(item->type_label), fields[2], field_end[2]);
    ha_remote_parse_catalog_field(item->capability, sizeof(item->capability), fields[3], field_end[3]);

    return item->entity_id[0] && item->label[0] && item->capability[0];
}

static bool ha_remote_parse_catalog(HaRemoteApp* app, const char* body) {
    app->add_item_count = 0;
    app->add_total_count = 0;

    const char* p = body;
    while(*p) {
        const char* token_start = p;
        while(*p && *p != '|') {
            p++;
        }
        const char* token_end = p;
        if(*p == '|') {
            p++;
        }

        if(token_end <= token_start) {
            continue;
        }

        if((token_end - token_start) > 2 && token_start[0] == 'T' && token_start[1] == '=') {
            ha_remote_parse_uint_token(token_start + 2, (size_t)(token_end - token_start - 2), &app->add_total_count);
            continue;
        }

        if((token_end - token_start) > 2 && token_start[0] == 'O' && token_start[1] == '=') {
            ha_remote_parse_uint_token(token_start + 2, (size_t)(token_end - token_start - 2), &app->add_offset);
            continue;
        }

        if(app->add_item_count < HA_REMOTE_ADD_ITEM_MAX &&
           ha_remote_parse_catalog_item(&app->add_items[app->add_item_count], token_start, token_end)) {
            app->add_item_count++;
        }
    }

    if(app->add_selected >= app->add_item_count && app->add_item_count > 0) {
        app->add_selected = app->add_item_count - 1;
    } else if(app->add_item_count == 0) {
        app->add_selected = 0;
    }

    return app->add_total_count > 0 || app->add_item_count > 0 || strstr(body, "T=0");
}

static void ha_remote_add_model_sync(HaRemoteApp* app, bool update) {
    if(!app->add_view) {
        return;
    }

    HaRemoteAddModel* model = view_get_model(app->add_view);
    snprintf(model->title, sizeof(model->title), "%s", ha_remote_add_category_label(app->add_category));
    if(app->add_loading) {
        snprintf(model->status, sizeof(model->status), "Loading");
    } else if(!app->add_load_ok && app->add_body[0] && app->add_mode == HaRemoteAddModeBrowser) {
        snprintf(model->status, sizeof(model->status), "%.31s", app->add_body);
    } else if(app->add_mode == HaRemoteAddModeBrowser && app->add_item_count == 0) {
        snprintf(model->status, sizeof(model->status), "No entries");
    } else {
        model->status[0] = '\0';
    }
    snprintf(model->draft_label, sizeof(model->draft_label), "%s", app->add_text_buffer);
    memcpy(model->items, app->add_items, sizeof(model->items));
    model->item_count = app->add_item_count;
    model->selected = app->add_selected;
    model->button = app->add_button;
    model->category = app->add_category;
    model->mode = app->add_mode;
    model->total_count = app->add_total_count;
    model->offset = app->add_offset;
    model->confirm_focus = app->add_confirm_focus;
    model->marquee_offset = app->marquee_offset;
    model->loading = app->add_loading;
    view_commit_model(app->add_view, update);
}

static int32_t ha_remote_add_load_worker(void* context) {
    HaRemoteApp* app = context;
    char command[HA_REMOTE_HTTP_COMMAND_MAX];
    char response[HA_REMOTE_HTTP_RESPONSE_MAX];
    char path[HA_REMOTE_COMMAND_PATH_MAX];

    snprintf(
        path,
        sizeof(path),
        "/v1/catalog/%s/%u",
        ha_remote_add_category_path(app->add_request_category),
        (unsigned)app->add_request_offset);
    app->add_load_ok = ha_remote_bridge_get_with_buffers(
        app,
        path,
        command,
        sizeof(command),
        response,
        sizeof(response),
        app->add_body,
        sizeof(app->add_body),
        false);
    view_dispatcher_send_custom_event(app->view_dispatcher, HaRemoteCustomEventAddLoadComplete);
    return 0;
}

static void ha_remote_join_add_load(HaRemoteApp* app) {
    if(app->add_load_thread) {
        furi_thread_join(app->add_load_thread);
        furi_thread_free(app->add_load_thread);
        app->add_load_thread = NULL;
    }
    app->add_loading = false;
}

static bool ha_remote_start_add_load(HaRemoteApp* app) {
    if(app->add_loading) {
        app->add_load_again = true;
        ha_remote_add_model_sync(app, true);
        return false;
    }

    app->add_body[0] = '\0';
    app->add_load_ok = false;
    app->add_item_count = 0;
    app->add_loading = true;
    app->add_load_again = false;
    app->add_request_category = app->add_category;
    app->add_request_offset = app->add_offset;
    ha_remote_add_model_sync(app, true);

    app->add_load_thread =
        furi_thread_alloc_ex("HA Add Load", 6144, ha_remote_add_load_worker, app);
    if(!app->add_load_thread) {
        app->add_loading = false;
        app->add_load_again = false;
        snprintf(app->add_body, sizeof(app->add_body), "Load failed");
        ha_remote_add_model_sync(app, true);
        return false;
    }

    furi_thread_start(app->add_load_thread);
    return true;
}

static void ha_remote_complete_add_load(HaRemoteApp* app) {
    bool ok = app->add_load_ok;
    bool restart = app->add_load_again;
    bool apply =
        app->add_mode == HaRemoteAddModeBrowser &&
        app->add_request_category == app->add_category &&
        app->add_request_offset == app->add_offset;
    char body[sizeof(app->add_body)];
    snprintf(body, sizeof(body), "%s", app->add_body);

    ha_remote_join_add_load(app);
    app->add_load_again = false;

    if(!apply) {
        app->add_load_ok = true;
        app->add_body[0] = '\0';
        if(restart && app->add_mode == HaRemoteAddModeBrowser) {
            ha_remote_start_add_load(app);
        } else {
            ha_remote_add_model_sync(app, true);
        }
        return;
    }

    if(ok && ha_remote_parse_catalog(app, body)) {
        app->add_load_ok = true;
    } else {
        app->add_load_ok = false;
        snprintf(app->add_body, sizeof(app->add_body), "%s", body[0] ? body : "Load failed");
    }

    ha_remote_add_model_sync(app, true);
    if(restart && app->add_mode == HaRemoteAddModeBrowser) {
        ha_remote_start_add_load(app);
    }
}

static bool ha_remote_controller_find_custom_by_entity(
    HaRemoteApp* app,
    const char* entity_id,
    uint8_t* custom_index) {
    for(uint8_t i = 0; i < app->custom_entry_count; i++) {
        if(app->custom_entries[i].used &&
           ha_remote_value_equals(app->custom_entries[i].entity_id, entity_id)) {
            if(custom_index) {
                *custom_index = i;
            }
            return true;
        }
    }
    return false;
}

static bool ha_remote_controller_move_entry_to_visible_bottom(HaRemoteApp* app, uint8_t entry_id) {
    uint8_t total_count = ha_remote_controller_total_entry_count(app);
    uint8_t slot = total_count;

    for(uint8_t i = 0; i < total_count; i++) {
        if(app->action_order[i] == entry_id) {
            slot = i;
            break;
        }
    }

    if(slot >= total_count) {
        return false;
    }

    for(uint8_t i = slot; i + 1 < total_count; i++) {
        app->action_order[i] = app->action_order[i + 1];
    }
    if(slot < app->action_order_count && app->action_order_count > 0) {
        app->action_order_count--;
    }

    for(uint8_t i = total_count - 1; i > app->action_order_count; i--) {
        app->action_order[i] = app->action_order[i - 1];
    }
    app->action_order[app->action_order_count] = entry_id;
    app->action_order_count++;
    return true;
}

static bool ha_remote_controller_add_custom_entry(
    HaRemoteApp* app,
    const HaRemoteCatalogItem* item,
    const char* label) {
    if(!item || !item->entity_id[0]) {
        return false;
    }

    uint8_t custom_index;
    bool existing =
        ha_remote_controller_find_custom_by_entity(app, item->entity_id, &custom_index);
    if(!existing) {
        if(app->custom_entry_count >= HA_REMOTE_CUSTOM_ENTRY_MAX) {
            return false;
        }
        custom_index = app->custom_entry_count;
        uint8_t new_entry_id = HA_REMOTE_ACTION_COUNT + custom_index;
        app->custom_entry_count++;
        app->action_order[ha_remote_controller_total_entry_count(app) - 1] = new_entry_id;
    }

    HaRemoteCustomEntry* entry = &app->custom_entries[custom_index];
    memset(entry, 0, sizeof(*entry));
    entry->used = 1;
    if(ha_remote_value_equals(item->capability, "dim")) {
        entry->kind = HaRemoteCustomKindDimmer;
    } else if(ha_remote_value_equals(item->capability, "run")) {
        entry->kind = HaRemoteCustomKindRoutine;
    } else {
        entry->kind = HaRemoteCustomKindToggle;
    }

    const char* display_label = label && label[0] ? label : item->label;
    ha_remote_copy_trimmed(entry->label, sizeof(entry->label), display_label, strlen(display_label));
    ha_remote_copy_trimmed(entry->entity_id, sizeof(entry->entity_id), item->entity_id, strlen(item->entity_id));
    ha_remote_copy_trimmed(
        entry->friendly_name,
        sizeof(entry->friendly_name),
        item->label,
        strlen(item->label));
    ha_remote_copy_trimmed(
        entry->type_label,
        sizeof(entry->type_label),
        item->type_label,
        strlen(item->type_label));

    if(!entry->label[0]) {
        ha_remote_copy_trimmed(entry->label, sizeof(entry->label), item->label, strlen(item->label));
    }

    if(entry->kind == HaRemoteCustomKindRoutine) {
        snprintf(app->custom_states[custom_index], sizeof(app->custom_states[custom_index]), "Run");
        app->custom_state_known[custom_index] = true;
    } else {
        snprintf(app->custom_states[custom_index], sizeof(app->custom_states[custom_index]), "--");
        app->custom_state_known[custom_index] = false;
    }

    uint8_t entry_id = HA_REMOTE_ACTION_COUNT + custom_index;
    if(!ha_remote_controller_move_entry_to_visible_bottom(app, entry_id)) {
        return false;
    }

    app->controller_selected = ha_remote_controller_item_count(app) - 1;
    app->controller_scroll = 0;
    return ha_remote_controller_order_save(app);
}

static bool ha_remote_select_thermostat_entity(HaRemoteApp* app, const HaRemoteCatalogItem* item) {
    if(!item || strncmp(item->entity_id, "climate.", 8) != 0) {
        return false;
    }

    snprintf(app->thermostat_entity_id, sizeof(app->thermostat_entity_id), "%s", item->entity_id);
    app->thermostat_configured = true;
    ha_remote_init_thermostat_state(app);
    bool saved = ha_remote_thermostat_config_save(app);
    ha_remote_sync_thermostat_model(app, false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_THERMOSTAT);
    ha_remote_set_header(app, saved ? "Thermostat Set" : "Save Failed", true);
    if(saved) {
        ha_remote_refresh_thermostat(app, false);
    } else {
        ha_remote_notify(app, &sequence_error);
    }
    return saved;
}

static void ha_remote_draw_add_header(Canvas* canvas, const char* title, const char* count) {
    ha_remote_draw_house_icon(canvas, 2, 0);
    ha_remote_draw_fit_str(canvas, 15, 9, count && count[0] ? 86 : 110, title);
    if(count && count[0]) {
        canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, count);
    }
    canvas_draw_line(canvas, 0, 11, 127, 11);
}

static void ha_remote_add_draw_icon_button(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    char icon,
    bool selected) {
    if(selected) {
        canvas_draw_box(canvas, x, y, 10, 10);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, x, y, 10, 10);
    }

    if(icon == 'i') {
        canvas_draw_box(canvas, x + 5, y + 2, 1, 1);
        canvas_draw_line(canvas, x + 5, y + 4, x + 5, y + 7);
        canvas_draw_line(canvas, x + 4, y + 7, x + 6, y + 7);
    } else {
        canvas_draw_line(canvas, x + 5, y + 2, x + 5, y + 8);
        canvas_draw_line(canvas, x + 2, y + 5, x + 8, y + 5);
    }

    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void ha_remote_add_draw_categories(Canvas* canvas, HaRemoteAddModel* model) {
    static const char* labels[] = {"Switches", "Lights", "Routines"};

    ha_remote_draw_add_header(canvas, "Add", "");
    for(uint8_t i = 0; i < HA_REMOTE_ADD_CONTROLLER_CATEGORY_COUNT; i++) {
        int32_t y = 16 + (i * 14);
        bool selected = model->selected == i;
        if(selected) {
            canvas_draw_box(canvas, 6, y - 1, 116, 12);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 12, y + 9, labels[i]);
        if(selected) {
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void ha_remote_add_draw_browser(Canvas* canvas, HaRemoteAddModel* model) {
    char count[12];
    if(model->total_count > 0) {
        snprintf(
            count,
            sizeof(count),
            "%u/%u",
            (unsigned)(model->offset + model->selected + 1),
            (unsigned)model->total_count);
    } else {
        count[0] = '\0';
    }

    ha_remote_draw_add_header(canvas, model->title, count);
    if(model->loading || model->status[0]) {
        canvas_draw_str_aligned(
            canvas,
            64,
            38,
            AlignCenter,
            AlignBottom,
            model->loading ? "Loading" : model->status);
        return;
    }

    for(uint8_t row = 0; row < HA_REMOTE_ADD_VISIBLE_ROWS; row++) {
        if(row >= model->item_count) {
            break;
        }

        int32_t y = 13 + (row * 12);
        bool row_selected = model->selected == row && model->button == HaRemoteAddButtonLabel;
        bool info_selected = model->selected == row && model->button == HaRemoteAddButtonInfo;
        bool add_selected = model->selected == row && model->button == HaRemoteAddButtonAdd;
        if(row_selected) {
            canvas_draw_box(canvas, 0, y, 101, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        ha_remote_draw_fit_str(canvas, 3, y + 9, 96, model->items[row].label);
        if(row_selected) {
            canvas_set_color(canvas, ColorBlack);
        }
        ha_remote_add_draw_icon_button(canvas, 104, y, 'i', info_selected);
        ha_remote_add_draw_icon_button(canvas, 117, y, '+', add_selected);
        canvas_draw_line(canvas, 0, y + 11, 127, y + 11);
    }
}

static void ha_remote_add_draw_info(Canvas* canvas, HaRemoteAddModel* model) {
    HaRemoteCatalogItem* item = model->selected < model->item_count ?
                                    &model->items[model->selected] :
                                    NULL;
    ha_remote_draw_add_header(canvas, "Entity Info", "");
    canvas_draw_frame(canvas, 4, 14, 120, 47);
    if(!item) {
        canvas_draw_str(canvas, 12, 35, "No entity");
        return;
    }

    canvas_draw_str(canvas, 10, 24, "Name");
    ha_remote_draw_fit_str(canvas, 39, 24, 80, item->label);
    canvas_draw_str(canvas, 10, 35, "Type");
    ha_remote_draw_fit_str(canvas, 39, 35, 80, item->type_label);
    canvas_draw_str(canvas, 10, 46, "Mode");
    ha_remote_draw_fit_str(canvas, 39, 46, 80, ha_remote_add_capability_label(item->capability));
    ha_remote_draw_marquee_str(canvas, 10, 57, 108, item->entity_id, model->marquee_offset);
}

static void ha_remote_add_draw_confirm_button(
    Canvas* canvas,
    int32_t x,
    const char* label,
    bool selected) {
    if(selected) {
        canvas_draw_box(canvas, x, 50, 45, 12);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, x, 50, 45, 12);
    }
    canvas_draw_str_aligned(canvas, x + 22, 60, AlignCenter, AlignBottom, label);
    if(selected) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void ha_remote_add_draw_confirm(Canvas* canvas, HaRemoteAddModel* model) {
    HaRemoteCatalogItem* item = model->selected < model->item_count ?
                                    &model->items[model->selected] :
                                    NULL;
    ha_remote_draw_add_header(canvas, "Add Row", "");
    canvas_draw_frame(canvas, 6, 15, 116, 13);
    if(model->confirm_focus == 0) {
        canvas_draw_frame(canvas, 8, 17, 112, 9);
    }
    ha_remote_draw_fit_str(canvas, 10, 25, 108, model->draft_label[0] ? model->draft_label : "Label");

    if(item) {
        ha_remote_draw_fit_str(canvas, 8, 39, 112, item->label);
        ha_remote_draw_fit_str(canvas, 8, 48, 112, item->entity_id);
    }

    ha_remote_add_draw_confirm_button(canvas, 16, "Cancel", model->confirm_focus == 1);
    ha_remote_add_draw_confirm_button(canvas, 67, "Add", model->confirm_focus == 2);
}

static void ha_remote_add_draw_callback(Canvas* canvas, void* model_context) {
    HaRemoteAddModel* model = model_context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(model->mode == HaRemoteAddModeCategories) {
        ha_remote_add_draw_categories(canvas, model);
    } else if(model->mode == HaRemoteAddModeInfo) {
        ha_remote_add_draw_info(canvas, model);
    } else if(model->mode == HaRemoteAddModeConfirm) {
        ha_remote_add_draw_confirm(canvas, model);
    } else {
        ha_remote_add_draw_browser(canvas, model);
    }
}

static void ha_remote_add_open_text(HaRemoteApp* app) {
    text_input_reset(app->add_text_input);
    text_input_set_header_text(app->add_text_input, "Entry Label");
    text_input_set_minimum_length(app->add_text_input, 1);
    text_input_set_result_callback(
        app->add_text_input,
        ha_remote_add_text_callback,
        app,
        app->add_text_buffer,
        sizeof(app->add_text_buffer),
        false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD_TEXT);
}

static void ha_remote_add_text_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    app->add_mode = HaRemoteAddModeConfirm;
    ha_remote_add_model_sync(app, false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD);
}

// Long-press OK on a custom row in Reorder mode opens this editable label box.
static void ha_remote_open_rename(HaRemoteApp* app, uint8_t entry_id) {
    if(ha_remote_entry_is_builtin(entry_id)) {
        return;
    }
    uint8_t custom_index = entry_id - HA_REMOTE_ACTION_COUNT;
    if(custom_index >= app->custom_entry_count) {
        return;
    }

    app->rename_active = true;
    app->rename_custom_index = custom_index;
    snprintf(
        app->add_text_buffer,
        sizeof(app->add_text_buffer),
        "%s",
        app->custom_entries[custom_index].label);

    text_input_reset(app->add_text_input);
    text_input_set_header_text(app->add_text_input, "Rename Row");
    text_input_set_minimum_length(app->add_text_input, 1);
    text_input_set_result_callback(
        app->add_text_input,
        ha_remote_rename_text_callback,
        app,
        app->add_text_buffer,
        sizeof(app->add_text_buffer),
        false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD_TEXT);
}

static void ha_remote_rename_text_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    app->rename_active = false;
    if(app->rename_custom_index < app->custom_entry_count) {
        snprintf(
            app->custom_entries[app->rename_custom_index].label,
            sizeof(app->custom_entries[app->rename_custom_index].label),
            "%s",
            app->add_text_buffer);
        ha_remote_controller_order_save(app);
    }

    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
    ha_remote_set_header(app, "Renamed", true);
    ha_remote_controller_model_sync(app, true);
}

static void ha_remote_add_open_confirm(HaRemoteApp* app) {
    HaRemoteCatalogItem* item = ha_remote_add_selected_item(app);
    if(!item) {
        return;
    }

    snprintf(app->add_text_buffer, sizeof(app->add_text_buffer), "%s", item->label);
    app->add_confirm_focus = 0;
    app->add_mode = HaRemoteAddModeConfirm;
    ha_remote_add_model_sync(app, true);
}

static void ha_remote_open_add(HaRemoteApp* app) {
    if(!ha_remote_enqueue_pending_command(app)) {
        return;
    }
    app->controller_tool_focus = false;
    app->add_origin = HaRemoteAddOriginController;
    app->add_mode = HaRemoteAddModeCategories;
    app->add_category = HaRemoteAddCategorySwitches;
    app->add_selected = 0;
    app->add_button = HaRemoteAddButtonLabel;
    app->add_offset = 0;
    app->add_total_count = 0;
    app->add_item_count = 0;
    app->add_confirm_focus = 0;
    app->add_load_ok = true;
    app->add_body[0] = '\0';
    app->add_text_buffer[0] = '\0';
    ha_remote_add_model_sync(app, false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD);
}

static void ha_remote_add_browser_move(HaRemoteApp* app, int8_t delta) {
    if(app->add_loading || app->add_item_count == 0) {
        return;
    }

    if(delta < 0) {
        if(app->add_selected > 0) {
            app->add_selected--;
        } else if(app->add_offset > 0) {
            app->add_offset = app->add_offset > HA_REMOTE_ADD_CATALOG_PAGE_SIZE ?
                                  app->add_offset - HA_REMOTE_ADD_CATALOG_PAGE_SIZE :
                                  0;
            app->add_selected = 0;
            ha_remote_start_add_load(app);
            return;
        }
    } else {
        if(app->add_selected + 1 < app->add_item_count) {
            app->add_selected++;
        } else if(app->add_offset + app->add_item_count < app->add_total_count) {
            app->add_offset += app->add_item_count;
            app->add_selected = 0;
            ha_remote_start_add_load(app);
            return;
        }
    }

    ha_remote_add_model_sync(app, true);
}

static bool ha_remote_add_go_back(HaRemoteApp* app) {
    if(app->add_mode == HaRemoteAddModeInfo || app->add_mode == HaRemoteAddModeConfirm) {
        app->add_mode = HaRemoteAddModeBrowser;
        ha_remote_add_model_sync(app, true);
        return true;
    }

    if(app->add_mode == HaRemoteAddModeBrowser) {
        if(app->add_origin == HaRemoteAddOriginThermostat) {
            ha_remote_switch_to_view(app, HA_REMOTE_VIEW_THERMOSTAT);
            ha_remote_sync_thermostat_model(app, false);
            return true;
        }
        app->add_mode = HaRemoteAddModeCategories;
        app->add_selected = app->add_category;
        ha_remote_add_model_sync(app, true);
        return true;
    }

    if(app->add_origin == HaRemoteAddOriginThermostat) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_THERMOSTAT);
        ha_remote_sync_thermostat_model(app, false);
    } else {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_controller_model_sync(app, true);
    }
    return true;
}

static bool ha_remote_add_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        return ha_remote_add_go_back(app);
    }

    if(app->add_mode == HaRemoteAddModeCategories) {
        if(event->key == InputKeyUp) {
            app->add_selected =
                (app->add_selected + HA_REMOTE_ADD_CONTROLLER_CATEGORY_COUNT - 1) % HA_REMOTE_ADD_CONTROLLER_CATEGORY_COUNT;
            ha_remote_add_model_sync(app, true);
            return true;
        }
        if(event->key == InputKeyDown) {
            app->add_selected = (app->add_selected + 1) % HA_REMOTE_ADD_CONTROLLER_CATEGORY_COUNT;
            ha_remote_add_model_sync(app, true);
            return true;
        }
        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            app->add_category = app->add_selected;
            app->add_selected = 0;
            app->add_button = HaRemoteAddButtonLabel;
            app->add_offset = 0;
            app->add_total_count = 0;
            app->add_mode = HaRemoteAddModeBrowser;
            ha_remote_add_model_sync(app, true);
            ha_remote_start_add_load(app);
            return true;
        }
        return true;
    }

    if(app->add_mode == HaRemoteAddModeInfo) {
        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            app->add_mode = HaRemoteAddModeBrowser;
            ha_remote_add_model_sync(app, true);
            return true;
        }
        return true;
    }

    if(app->add_mode == HaRemoteAddModeConfirm) {
        if(event->key == InputKeyUp) {
            app->add_confirm_focus = app->add_confirm_focus == 0 ? 2 : app->add_confirm_focus - 1;
            ha_remote_add_model_sync(app, true);
            return true;
        }
        if(event->key == InputKeyDown) {
            app->add_confirm_focus = (app->add_confirm_focus + 1) % 3;
            ha_remote_add_model_sync(app, true);
            return true;
        }
        if(event->key == InputKeyLeft || event->key == InputKeyRight) {
            if(app->add_confirm_focus > 0) {
                app->add_confirm_focus = app->add_confirm_focus == 1 ? 2 : 1;
                ha_remote_add_model_sync(app, true);
            }
            return true;
        }
        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            if(app->add_confirm_focus == 0) {
                ha_remote_add_open_text(app);
                return true;
            }
            if(app->add_confirm_focus == 1) {
                app->add_mode = HaRemoteAddModeBrowser;
                ha_remote_add_model_sync(app, true);
                return true;
            }

            HaRemoteCatalogItem* item = ha_remote_add_selected_item(app);
            if(app->add_origin == HaRemoteAddOriginThermostat) {
                if(!ha_remote_select_thermostat_entity(app, item)) {
                    snprintf(app->add_body, sizeof(app->add_body), "Add failed");
                    app->add_load_ok = false;
                    ha_remote_add_model_sync(app, true);
                    ha_remote_notify(app, &sequence_error);
                }
                return true;
            }

            if(item && ha_remote_controller_add_custom_entry(app, item, app->add_text_buffer)) {
                ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
                ha_remote_set_header(app, "Row Added", true);
                ha_remote_controller_model_sync(app, true);
            } else {
                snprintf(app->add_body, sizeof(app->add_body), "Add failed");
                app->add_load_ok = false;
                ha_remote_add_model_sync(app, true);
                ha_remote_notify(app, &sequence_error);
            }
            return true;
        }
        return true;
    }

    if(app->add_loading) {
        return true;
    }

    if(event->key == InputKeyUp) {
        ha_remote_add_browser_move(app, -1);
        return true;
    }
    if(event->key == InputKeyDown) {
        ha_remote_add_browser_move(app, 1);
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyLeft) {
        if(app->add_button > HaRemoteAddButtonLabel) {
            app->add_button--;
            ha_remote_add_model_sync(app, true);
        }
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyRight) {
        if(app->add_button < HaRemoteAddButtonAdd) {
            app->add_button++;
            ha_remote_add_model_sync(app, true);
        }
        return true;
    }
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(app->add_button == HaRemoteAddButtonInfo) {
            app->add_mode = HaRemoteAddModeInfo;
            HaRemoteCatalogItem* info_item = ha_remote_add_selected_item(app);
            ha_remote_marquee_start(app, info_item ? info_item->entity_id : "");
            ha_remote_add_model_sync(app, true);
        } else if(app->add_origin == HaRemoteAddOriginThermostat) {
            HaRemoteCatalogItem* item = ha_remote_add_selected_item(app);
            if(!ha_remote_select_thermostat_entity(app, item)) {
                snprintf(app->add_body, sizeof(app->add_body), "Add failed");
                app->add_load_ok = false;
                ha_remote_add_model_sync(app, true);
                ha_remote_notify(app, &sequence_error);
            }
        } else {
            ha_remote_add_open_confirm(app);
        }
        return true;
    }

    return true;
}

static void ha_remote_controller_select_action_relative(HaRemoteApp* app, int8_t delta) {
    uint8_t first = HA_REMOTE_ACTION_MENU_BASE;
    uint8_t item_count = ha_remote_controller_item_count(app);
    if(item_count <= HA_REMOTE_ACTION_MENU_BASE) {
        app->controller_selected = HA_REMOTE_REFRESH_INDEX;
        app->controller_reorder_delete_focus = false;
        return;
    }

    uint8_t last = item_count - 1;

    if(app->controller_selected < first || app->controller_selected > last) {
        app->controller_selected = delta < 0 ? last : first;
        return;
    }

    if(delta < 0) {
        app->controller_selected = app->controller_selected == first ? last :
                                                                    app->controller_selected - 1;
    } else {
        app->controller_selected = app->controller_selected == last ? first :
                                                                   app->controller_selected + 1;
    }
}

static void ha_remote_controller_move_selected_to_slot(HaRemoteApp* app, uint8_t to_slot) {
    if(app->controller_selected < HA_REMOTE_ACTION_MENU_BASE ||
       to_slot >= app->action_order_count) {
        return;
    }

    uint8_t from_slot = app->controller_selected - HA_REMOTE_ACTION_MENU_BASE;
    if(from_slot >= app->action_order_count || from_slot == to_slot) {
        return;
    }

    uint8_t moved = app->action_order[from_slot];
    if(from_slot < to_slot) {
        memmove(
            &app->action_order[from_slot],
            &app->action_order[from_slot + 1],
            to_slot - from_slot);
    } else {
        memmove(
            &app->action_order[to_slot + 1],
            &app->action_order[to_slot],
            from_slot - to_slot);
    }
    app->action_order[to_slot] = moved;
    app->controller_selected = to_slot + HA_REMOTE_ACTION_MENU_BASE;
}

static void ha_remote_controller_move_selected_by(HaRemoteApp* app, int8_t delta) {
    if(app->controller_selected < HA_REMOTE_ACTION_MENU_BASE) {
        return;
    }

    int16_t from_slot = app->controller_selected - HA_REMOTE_ACTION_MENU_BASE;
    int16_t to_slot = from_slot + delta;
    if(to_slot < 0 || to_slot >= (int16_t)app->action_order_count) {
        return;
    }

    ha_remote_controller_move_selected_to_slot(app, (uint8_t)to_slot);
}

static bool ha_remote_controller_delete_selected(HaRemoteApp* app) {
    if(app->controller_selected < HA_REMOTE_ACTION_MENU_BASE || app->action_order_count == 0) {
        return false;
    }

    uint8_t slot = app->controller_selected - HA_REMOTE_ACTION_MENU_BASE;
    if(slot >= app->action_order_count) {
        return false;
    }

    uint8_t deleted = app->action_order[slot];
    if(slot + 1 < app->action_order_count) {
        memmove(
            &app->action_order[slot],
            &app->action_order[slot + 1],
            app->action_order_count - slot - 1);
    }
    app->action_order_count--;
    app->action_order[app->action_order_count] = deleted;

    if(app->action_order_count == 0) {
        app->controller_selected = HA_REMOTE_REFRESH_INDEX;
        app->controller_reorder_delete_focus = false;
    } else if(slot >= app->action_order_count) {
        app->controller_selected = HA_REMOTE_ACTION_MENU_BASE + app->action_order_count - 1;
    }

    return true;
}

static void ha_remote_controller_enter_reorder(HaRemoteApp* app) {
    if(app->action_order_count == 0) {
        app->controller_tool_focus = false;
        ha_remote_set_header(app, "No rows yet", true);
        ha_remote_controller_model_sync(app, true);
        return;
    }
    app->controller_tool_focus = false;
    app->controller_tool_selected = HA_REMOTE_CONTROLLER_TOOL_REORDER;
    app->controller_reorder_mode = true;
    app->controller_reorder_grabbed = false;
    app->controller_reorder_delete_focus = false;
    app->controller_reorder_delete_confirm = false;

    if(app->controller_selected < HA_REMOTE_ACTION_MENU_BASE && app->action_order_count > 0) {
        app->controller_selected = HA_REMOTE_ACTION_MENU_BASE;
    }

    ha_remote_set_header(app, "Reorder", false);
}

static void ha_remote_controller_exit_reorder(HaRemoteApp* app) {
    bool saved = ha_remote_controller_order_save(app);

    app->controller_tool_focus = false;
    app->controller_reorder_mode = false;
    app->controller_reorder_grabbed = false;
    app->controller_reorder_delete_focus = false;
    app->controller_reorder_delete_confirm = false;
    ha_remote_set_header(app, saved ? "Order Saved" : "Save Failed", true);
    if(!saved) {
        ha_remote_notify(app, &sequence_error);
    }
}

static void ha_remote_open_controller_info(HaRemoteApp* app) {
    app->controller_tool_focus = false;
    ha_remote_controller_info_model_sync(app, false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER_INFO);
}

static bool ha_remote_controller_reorder_input(HaRemoteApp* app, InputEvent* event) {
    if(app->controller_reorder_delete_confirm) {
        if(event->type == InputTypeShort && event->key == InputKeyBack) {
            app->controller_reorder_delete_confirm = false;
            ha_remote_controller_model_sync(app, true);
            return true;
        }

        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            ha_remote_controller_delete_selected(app);
            app->controller_reorder_delete_confirm = false;
            app->controller_reorder_delete_focus = false;
            ha_remote_controller_model_sync(app, true);
            return true;
        }

        return event->type == InputTypeShort || event->type == InputTypeRepeat;
    }

    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        ha_remote_controller_exit_reorder(app);
        return true;
    }

    if(event->type == InputTypeLong && event->key == InputKeyOk &&
       !app->controller_reorder_grabbed && !app->controller_reorder_delete_focus &&
       app->controller_selected >= HA_REMOTE_ACTION_MENU_BASE) {
        uint8_t entry_id;
        if(ha_remote_controller_entry_id_for_item(app, app->controller_selected, &entry_id) &&
           !ha_remote_entry_is_builtin(entry_id)) {
            ha_remote_open_rename(app, entry_id);
        }
        return true;
    }

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(app->controller_reorder_delete_focus) {
            app->controller_reorder_delete_confirm = true;
            ha_remote_controller_model_sync(app, true);
            return true;
        }

        if(app->controller_selected >= HA_REMOTE_ACTION_MENU_BASE && app->action_order_count > 0) {
            app->controller_reorder_grabbed = !app->controller_reorder_grabbed;
            if(app->controller_reorder_grabbed) {
                app->controller_reorder_delete_focus = false;
            }
        }
        ha_remote_controller_model_sync(app, true);
        return true;
    }

    if(app->controller_reorder_grabbed) {
        if(event->key == InputKeyUp) {
            ha_remote_controller_move_selected_by(app, -1);
            ha_remote_controller_model_sync(app, true);
            return true;
        } else if(event->key == InputKeyDown) {
            ha_remote_controller_move_selected_by(app, 1);
            ha_remote_controller_model_sync(app, true);
            return true;
        } else if(event->key == InputKeyLeft) {
            ha_remote_controller_move_selected_to_slot(app, 0);
            ha_remote_controller_model_sync(app, true);
            return true;
        } else if(event->key == InputKeyRight && app->action_order_count > 0) {
            ha_remote_controller_move_selected_to_slot(app, app->action_order_count - 1);
            ha_remote_controller_model_sync(app, true);
            return true;
        }
    } else if(event->key == InputKeyUp || event->key == InputKeyDown) {
        ha_remote_controller_select_action_relative(app, event->key == InputKeyUp ? -1 : 1);
        ha_remote_controller_model_sync(app, true);
        return true;
    } else if(event->type == InputTypeShort && event->key == InputKeyRight) {
        if(app->controller_selected >= HA_REMOTE_ACTION_MENU_BASE && app->action_order_count > 0) {
            app->controller_reorder_delete_focus = true;
            ha_remote_controller_model_sync(app, true);
        }
        return true;
    } else if(event->type == InputTypeShort && event->key == InputKeyLeft) {
        if(app->controller_reorder_delete_focus) {
            app->controller_reorder_delete_focus = false;
            ha_remote_controller_model_sync(app, true);
        }
        return true;
    }

    return true;
}

static bool ha_remote_controller_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(app->controller_reorder_mode) {
        return ha_remote_controller_reorder_input(app, event);
    }

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(app->controller_tool_focus) {
            if(event->key == InputKeyUp || event->key == InputKeyDown) {
                if(event->key == InputKeyUp) {
                    app->controller_tool_selected =
                        (app->controller_tool_selected + HA_REMOTE_CONTROLLER_TOOL_COUNT - 1) %
                        HA_REMOTE_CONTROLLER_TOOL_COUNT;
                } else {
                    app->controller_tool_selected =
                        (app->controller_tool_selected + 1) % HA_REMOTE_CONTROLLER_TOOL_COUNT;
                }
                ha_remote_controller_model_sync(app, true);
                return true;
            } else if(event->type == InputTypeShort && event->key == InputKeyRight) {
                app->controller_tool_focus = false;
                ha_remote_controller_model_sync(app, true);
                return true;
            } else if(event->type == InputTypeShort && event->key == InputKeyLeft) {
                return true;
            }
        } else if(event->key == InputKeyUp) {
            uint8_t item_count = ha_remote_controller_item_count(app);
            app->controller_selected =
                (app->controller_selected + item_count - 1) % item_count;
            ha_remote_controller_model_sync(app, true);
            return true;
        } else if(event->key == InputKeyDown) {
            uint8_t item_count = ha_remote_controller_item_count(app);
            app->controller_selected = (app->controller_selected + 1) % item_count;
            ha_remote_controller_model_sync(app, true);
            return true;
        } else if(event->type == InputTypeShort && event->key == InputKeyLeft) {
            app->controller_tool_focus = true;
            app->controller_tool_selected = HA_REMOTE_CONTROLLER_TOOL_REORDER;
            ha_remote_controller_model_sync(app, true);
            return true;
        }
    }

    if(app->controller_tool_focus &&
       (event->type == InputTypeShort || event->type == InputTypeLong) &&
       event->key == InputKeyOk) {
        if(event->type == InputTypeShort) {
            if(app->controller_tool_selected == HA_REMOTE_CONTROLLER_TOOL_INFO) {
                ha_remote_open_controller_info(app);
            } else if(app->controller_tool_selected == HA_REMOTE_CONTROLLER_TOOL_REORDER) {
                ha_remote_controller_enter_reorder(app);
            } else if(app->controller_tool_selected == HA_REMOTE_CONTROLLER_TOOL_PLUS) {
                ha_remote_open_add(app);
            }
        }
        return true;
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        ha_remote_controller_callback(app, InputTypeShort, app->controller_selected);
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
            ha_remote_controller_model_sync(app, true);
        }
        return true;
    }

    if(event->type == InputTypeLong && event->key == InputKeyOk) {
        ha_remote_controller_callback(app, InputTypeLong, app->controller_selected);
        if(app->current_view == HA_REMOTE_VIEW_CONTROLLER) {
            ha_remote_controller_model_sync(app, true);
        }
        return true;
    }

    return false;
}

static void ha_remote_root_draw_callback(Canvas* canvas, void* model_context) {
    static const char* labels[] = {"Controller", "Thermostat", "Settings", "About"};
    HaRemoteRootModel* model = model_context;

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    ha_remote_draw_house_icon(canvas, 2, 0);
    canvas_draw_str(canvas, 15, 9, model->header[0] ? model->header : "FlipperHA");
    canvas_draw_line(canvas, 0, 11, 127, 11);

    for(uint8_t i = 0; i < HA_REMOTE_ROOT_ITEM_COUNT; i++) {
        int32_t y = 14 + (i * 12);
        bool selected = model->selected == i;
        if(selected) {
            canvas_draw_box(canvas, 0, y, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 7, y + 9, labels[i]);
        if(selected) {
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static bool ha_remote_root_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            app->root_selected =
                (app->root_selected + HA_REMOTE_ROOT_ITEM_COUNT - 1) % HA_REMOTE_ROOT_ITEM_COUNT;
            ha_remote_root_model_sync(app, true);
            return true;
        } else if(event->key == InputKeyDown) {
            app->root_selected = (app->root_selected + 1) % HA_REMOTE_ROOT_ITEM_COUNT;
            ha_remote_root_model_sync(app, true);
            return true;
        }
    }

    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        ha_remote_root_callback(app, InputTypeShort, app->root_selected);
        return true;
    }

    return false;
}

static void ha_remote_draw_control(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    size_t width,
    const char* label,
    bool focused,
    bool editing) {
    if(focused && editing) {
        canvas_draw_box(canvas, x, y, width, 13);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_frame(canvas, x, y, width, 13);
        if(focused) {
            canvas_draw_frame(canvas, x + 2, y + 2, width - 4, 9);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + (int32_t)(width / 2), y + 10, AlignCenter, AlignBottom, label);

    if(focused && editing) {
        canvas_set_color(canvas, ColorBlack);
    }
}

static void ha_remote_draw_option_carousel(
    Canvas* canvas,
    int32_t x,
    int32_t button_y,
    size_t width,
    const HaRemoteThermoOption* options,
    uint8_t option_count,
    uint8_t selected) {
    const int32_t row_h = 8;
    int32_t height = (option_count * row_h) + 2;
    int32_t y = button_y - height;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, width, height + 1);

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, width, height + 1);
    canvas_draw_line(
        canvas,
        x + (int32_t)(width / 2),
        button_y - 1,
        x + (int32_t)(width / 2),
        button_y);
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0; i < option_count; i++) {
        int32_t row_y = y + 1 + (i * row_h);
        if(i == selected) {
            canvas_draw_box(canvas, x + 1, row_y, width - 2, row_h);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_draw_str_aligned(
            canvas,
            x + (int32_t)(width / 2),
            row_y + 7,
            AlignCenter,
            AlignBottom,
            options[i].label);

        if(i == selected) {
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void ha_remote_draw_droplet_icon(Canvas* canvas, int32_t x, int32_t y) {
    static const uint8_t rows[] = {
        0x08,
        0x14,
        0x22,
        0x22,
        0x59,
        0x59,
        0x45,
        0x22,
        0x1C,
    };

    for(uint8_t row = 0; row < COUNT_OF(rows); row++) {
        for(uint8_t col = 0; col < 7; col++) {
            if(rows[row] & (1 << (6 - col))) {
                canvas_draw_dot(canvas, x + col, y + row);
            }
        }
    }
}

static void ha_remote_draw_degree_icon(Canvas* canvas, int32_t x, int32_t y) {
    static const uint8_t rows[] = {0x07, 0x05, 0x07};

    for(uint8_t row = 0; row < COUNT_OF(rows); row++) {
        for(uint8_t col = 0; col < 3; col++) {
            if(rows[row] & (1 << (2 - col))) {
                canvas_draw_dot(canvas, x + col, y + row);
            }
        }
    }
}

static void ha_remote_thermostat_draw_callback(Canvas* canvas, void* model_context) {
    HaRemoteThermostatModel* model = model_context;
    char status[HA_REMOTE_HEADER_MAX];
    char action[HA_REMOTE_THERMO_VALUE_MAX];
    const char* actual_text;
    char setpoint[8];
    char humidity[8];
    char mode_button[16];
    char fan_button[16];
    char save_button[8];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(!model->configured) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, "Thermostat Setup");
        canvas_draw_line(canvas, 0, 10, 127, 10);
        canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignBottom, "No climate set");
        canvas_draw_str_aligned(canvas, 64, 43, AlignCenter, AlignBottom, "OK to choose");
        return;
    }

    if(model->status[0] && !ha_remote_value_equals(model->status, "Thermostat")) {
        snprintf(status, sizeof(status), "%.31s", model->status);
    } else {
        ha_remote_title_case(action, sizeof(action), model->action);
        snprintf(
            status,
            sizeof(status),
            model->fan_supported ? "%.12s / %.12s" : "%.24s",
            action,
            model->fan_supported ? ha_remote_thermo_fans[model->draft_fan].value : "");
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, status);
    canvas_draw_line(canvas, 0, 10, 127, 10);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 3, 22, "Actual");
    canvas_set_font(canvas, FontPrimary);
    actual_text = model->known ? model->actual : "--";
    canvas_draw_str_aligned(canvas, 16, 35, AlignCenter, AlignBottom, actual_text);
    ha_remote_draw_degree_icon(canvas, 17 + ((int32_t)canvas_string_width(canvas, actual_text) / 2), 26);
    canvas_set_font(canvas, FontSecondary);
    snprintf(humidity, sizeof(humidity), "%.4s%%", model->known ? model->humidity : "--");
    ha_remote_draw_droplet_icon(canvas, 3, 38);
    canvas_draw_str(canvas, 12, 46, humidity);

    if(model->focus == HaRemoteThermoFocusSetpoint) {
        canvas_draw_frame(canvas, 38, 13, 52, 35);
        if(model->editing) {
            canvas_draw_frame(canvas, 40, 15, 48, 31);
        }
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignBottom, "SET");
    canvas_draw_str(canvas, 40, 35, "<");
    canvas_draw_str(canvas, 84, 35, ">");
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(setpoint, sizeof(setpoint), "%d", (int)model->draft_target);
    canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignBottom, model->known ? setpoint : "--");
    if(ha_remote_thermostat_target_dirty(model)) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 91, 22, "*");
    }

    snprintf(
        mode_button,
        sizeof(mode_button),
        "M:%s%s",
        ha_remote_thermo_modes[model->draft_mode].label,
        ha_remote_thermostat_mode_dirty(model) ? "*" : "");
    snprintf(
        fan_button,
        sizeof(fan_button),
        "F:%s%s",
        model->fan_supported ? ha_remote_thermo_fans[model->draft_fan].label : "--",
        ha_remote_thermostat_fan_dirty(model) ? "*" : "");
    snprintf(save_button, sizeof(save_button), "Save%s", model->dirty ? "*" : "");

    if(model->editing && model->focus == HaRemoteThermoFocusMode) {
        ha_remote_draw_option_carousel(
            canvas,
            0,
            51,
            39,
            ha_remote_thermo_modes,
            HA_REMOTE_THERMO_MODE_COUNT,
            model->draft_mode);
    } else if(model->editing && model->focus == HaRemoteThermoFocusFan && model->fan_supported) {
        ha_remote_draw_option_carousel(
            canvas,
            44,
            51,
            39,
            ha_remote_thermo_fans,
            HA_REMOTE_THERMO_FAN_COUNT,
            model->draft_fan);
    }

    ha_remote_draw_control(
        canvas,
        0,
        51,
        39,
        mode_button,
        model->focus == HaRemoteThermoFocusMode,
        model->editing);
    ha_remote_draw_control(
        canvas, 44, 51, 39, fan_button, model->focus == HaRemoteThermoFocusFan, model->editing);
    ha_remote_draw_control(
        canvas, 89, 51, 39, save_button, model->focus == HaRemoteThermoFocusSave, false);
}

static void ha_remote_update_settings_labels(HaRemoteApp* app) {
    snprintf(
        app->vibration_label,
        sizeof(app->vibration_label),
        "Vibration [%s]",
        app->vibration_enabled ? "on" : "off");
    snprintf(
        app->log_mode_label,
        sizeof(app->log_mode_label),
        "Log [%s]",
        ha_remote_log_mode_labels[app->log_mode]);

    submenu_change_item_label(
        app->settings_menu, HA_REMOTE_SETTINGS_VIBRATION_INDEX, app->vibration_label);
    submenu_change_item_label(
        app->settings_menu, HA_REMOTE_SETTINGS_LOG_INDEX, app->log_mode_label);
}

static bool ha_remote_refresh_thermostat(HaRemoteApp* app, bool notify) {
    if(!app->thermostat_configured) {
        ha_remote_thermostat_set_status(app, "Setup needed");
        ha_remote_set_header(app, "Setup needed", true);
        return false;
    }

    char path[HA_REMOTE_COMMAND_PATH_MAX];
    snprintf(path, sizeof(path), "/v1/thermostat/state/%s", app->thermostat_entity_id);
    ha_remote_loading_start(app, "Thermostat");
    bool ok = ha_remote_bridge_get(app, path, app->http_body, sizeof(app->http_body));

    if(ok && ha_remote_parse_thermostat(app, app->http_body)) {
        ha_remote_set_header(app, "Thermostat updated", true);
        if(ha_remote_log_debug_enabled(app)) {
            FURI_LOG_D(HA_REMOTE_LOG_TAG, "Thermostat refresh OK: %s", app->http_body);
        }
        if(notify) {
            ha_remote_notify(app, &sequence_single_vibro);
        }
        return true;
    }

    if(ha_remote_log_error_enabled(app)) {
        FURI_LOG_W(
            HA_REMOTE_LOG_TAG,
            "Thermostat refresh failed: %s",
            app->http_body[0] ? app->http_body : "unknown error");
    }
    ha_remote_set_header(app, app->http_body[0] ? app->http_body : "Refresh failed", true);
    if(notify) {
        ha_remote_notify(app, &sequence_error);
    }
    return false;
}

static void ha_remote_thermostat_move_focus(HaRemoteThermostatModel* model, int8_t delta) {
    int8_t focus = (int8_t)model->focus + delta;
    if(focus < 0) {
        focus = (int8_t)HaRemoteThermoFocusSave;
    } else if(focus > (int8_t)HaRemoteThermoFocusSave) {
        focus = 0;
    }
    model->focus = (HaRemoteThermoFocus)focus;
    model->editing = false;
}

static void ha_remote_thermostat_move_bottom_focus(HaRemoteThermostatModel* model, int8_t delta) {
    int8_t focus = (int8_t)model->focus;
    if(model->focus == HaRemoteThermoFocusSetpoint) {
        focus = (int8_t)HaRemoteThermoFocusMode;
    } else {
        focus += delta;
    }

    if(focus < (int8_t)HaRemoteThermoFocusMode) {
        focus = (int8_t)HaRemoteThermoFocusSave;
    } else if(focus > (int8_t)HaRemoteThermoFocusSave) {
        focus = (int8_t)HaRemoteThermoFocusMode;
    }

    model->focus = (HaRemoteThermoFocus)focus;
    model->editing = false;
}

static void ha_remote_thermostat_focus_setpoint(HaRemoteThermostatModel* model) {
    model->focus = HaRemoteThermoFocusSetpoint;
    model->editing = false;
}

static void ha_remote_thermostat_change_target(HaRemoteThermostatModel* model, int8_t delta) {
    int16_t target = model->draft_target + delta;
    if(target < 45) {
        target = 45;
    } else if(target > 95) {
        target = 95;
    }

    if(target != model->draft_target) {
        model->draft_target = target;
        ha_remote_thermostat_recompute_dirty(model);
    }
}

static void ha_remote_thermostat_cycle_mode(HaRemoteThermostatModel* model, int8_t delta) {
    model->draft_mode = ha_remote_thermo_cycle_index(
        model->draft_mode, delta, model->mode_mask, HA_REMOTE_THERMO_MODE_COUNT);
    ha_remote_thermostat_recompute_dirty(model);
}

static void ha_remote_thermostat_cycle_fan(HaRemoteThermostatModel* model, int8_t delta) {
    if(!model->fan_supported) {
        return;
    }
    model->draft_fan = ha_remote_thermo_cycle_index(
        model->draft_fan, delta, model->fan_mask, HA_REMOTE_THERMO_FAN_COUNT);
    ha_remote_thermostat_recompute_dirty(model);
}

static void ha_remote_thermostat_begin_edit(HaRemoteThermostatModel* model) {
    if(model->focus == HaRemoteThermoFocusMode) {
        model->edit_mode_start = model->draft_mode;
        model->editing = true;
    } else if(model->focus == HaRemoteThermoFocusFan && model->fan_supported) {
        model->edit_fan_start = model->draft_fan;
        model->editing = true;
    }
}

static bool ha_remote_thermostat_cancel_edit(HaRemoteThermostatModel* model) {
    if(model->focus == HaRemoteThermoFocusMode) {
        model->draft_mode = model->edit_mode_start;
    } else if(model->focus == HaRemoteThermoFocusFan) {
        model->draft_fan = model->edit_fan_start;
    } else {
        return false;
    }

    model->editing = false;
    ha_remote_thermostat_recompute_dirty(model);
    return true;
}

static void ha_remote_thermostat_save(HaRemoteApp* app) {
    char path[HA_REMOTE_COMMAND_PATH_MAX];
    bool dirty = false;

    HaRemoteThermostatModel* model = view_get_model(app->thermostat_view);
    dirty = model->dirty;
    if(!app->thermostat_configured) {
        ha_remote_set_header(app, "Setup needed", true);
        return;
    }

    snprintf(
        path,
        sizeof(path),
        "/v1/thermostat/apply/%s/%d/%s/%s",
        app->thermostat_entity_id,
        (int)model->draft_target,
        ha_remote_thermo_modes[model->draft_mode].path_value,
        model->fan_supported ? ha_remote_thermo_fans[model->draft_fan].path_value : "none");
    view_commit_model(app->thermostat_view, false);

    if(dirty) {
        ha_remote_schedule_thermostat_command(app, path);
    } else {
        ha_remote_drop_pending_command(app);
        ha_remote_refresh_thermostat(app, true);
    }
}

static void ha_remote_open_thermostat_setup(HaRemoteApp* app) {
    app->add_origin = HaRemoteAddOriginThermostat;
    app->add_mode = HaRemoteAddModeBrowser;
    app->add_category = HaRemoteAddCategoryClimate;
    app->add_selected = 0;
    app->add_button = HaRemoteAddButtonLabel;
    app->add_offset = 0;
    app->add_total_count = 0;
    app->add_item_count = 0;
    app->add_confirm_focus = 0;
    app->add_load_ok = true;
    app->add_body[0] = '\0';
    app->add_text_buffer[0] = '\0';
    ha_remote_add_model_sync(app, false);
    ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD);
    ha_remote_start_add_load(app);
}

static bool ha_remote_thermostat_input_callback(InputEvent* event, void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(event->type != InputTypeShort && event->type != InputTypeRepeat) {
        return false;
    }

    if(!app->thermostat_configured) {
        if(event->type == InputTypeShort && event->key == InputKeyOk) {
            ha_remote_open_thermostat_setup(app);
            return true;
        }
        return event->key != InputKeyBack;
    }

    bool update = true;
    bool save = false;
    HaRemoteThermostatModel* model = view_get_model(app->thermostat_view);

    if(model->editing) {
        if(event->type == InputTypeShort && event->key == InputKeyBack) {
            update = ha_remote_thermostat_cancel_edit(model);
        } else if(event->key == InputKeyOk) {
            model->editing = false;
        } else if(event->key == InputKeyUp || event->key == InputKeyRight) {
            if(model->focus == HaRemoteThermoFocusSetpoint) {
                ha_remote_thermostat_change_target(model, 1);
            } else if(model->focus == HaRemoteThermoFocusMode) {
                ha_remote_thermostat_cycle_mode(model, event->key == InputKeyUp ? -1 : 1);
            } else if(model->focus == HaRemoteThermoFocusFan) {
                ha_remote_thermostat_cycle_fan(model, event->key == InputKeyUp ? -1 : 1);
            } else {
                update = false;
            }
        } else if(event->key == InputKeyDown || event->key == InputKeyLeft) {
            if(model->focus == HaRemoteThermoFocusSetpoint) {
                ha_remote_thermostat_change_target(model, -1);
            } else if(model->focus == HaRemoteThermoFocusMode) {
                ha_remote_thermostat_cycle_mode(model, event->key == InputKeyDown ? 1 : -1);
            } else if(model->focus == HaRemoteThermoFocusFan) {
                ha_remote_thermostat_cycle_fan(model, event->key == InputKeyDown ? 1 : -1);
            } else {
                update = false;
            }
        } else {
            update = false;
        }
    } else if(model->focus == HaRemoteThermoFocusSetpoint) {
        if(event->key == InputKeyLeft) {
            ha_remote_thermostat_change_target(model, -1);
        } else if(event->key == InputKeyRight) {
            ha_remote_thermostat_change_target(model, 1);
        } else if(event->key == InputKeyUp) {
            ha_remote_thermostat_move_focus(model, -1);
        } else if(event->key == InputKeyDown) {
            ha_remote_thermostat_move_focus(model, 1);
        } else {
            update = false;
        }
    } else if(event->key == InputKeyUp || event->key == InputKeyDown) {
        ha_remote_thermostat_focus_setpoint(model);
    } else if(event->key == InputKeyLeft) {
        ha_remote_thermostat_move_bottom_focus(model, -1);
    } else if(event->key == InputKeyRight) {
        ha_remote_thermostat_move_bottom_focus(model, 1);
    } else if(event->key == InputKeyOk) {
        if(model->focus == HaRemoteThermoFocusSave) {
            save = true;
        } else {
            ha_remote_thermostat_begin_edit(model);
        }
    } else {
        update = false;
    }

    view_commit_model(app->thermostat_view, update && !save);

    if(save) {
        ha_remote_thermostat_save(app);
    }

    return update;
}

static void ha_remote_switch_to_view(HaRemoteApp* app, uint32_t view_id) {
    app->current_view = view_id;
    ha_remote_set_header(app, ha_remote_current_default_header(app), false);
    view_dispatcher_switch_to_view(app->view_dispatcher, view_id);
}

static void ha_remote_root_callback(void* context, InputType input_type, uint32_t index) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(input_type != InputTypeShort && input_type != InputTypeLong) {
        return;
    }

    if(index == HA_REMOTE_ROOT_CONTROLLER_INDEX) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_start_controller_sync(app, false);
    } else if(index == HA_REMOTE_ROOT_THERMOSTAT_INDEX) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_THERMOSTAT);
        ha_remote_init_thermostat_state(app);
        ha_remote_sync_thermostat_model(app, false);
        if(!app->thermostat_configured) {
            ha_remote_set_header(app, "Setup needed", true);
        } else if(app->controller_sync_running) {
            ha_remote_set_header(app, "Sync Busy", true);
        } else {
            ha_remote_refresh_thermostat(app, false);
        }
    } else if(index == HA_REMOTE_ROOT_SETTINGS_INDEX) {
        ha_remote_update_settings_labels(app);
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_SETTINGS);
    } else if(index == HA_REMOTE_ROOT_ABOUT_INDEX) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ABOUT);
    }
}

static void ha_remote_settings_callback(void* context, InputType input_type, uint32_t index) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(input_type != InputTypeShort && input_type != InputTypeLong) {
        return;
    }

    if(index == HA_REMOTE_SETTINGS_VIBRATION_INDEX) {
        app->vibration_enabled = !app->vibration_enabled;
        ha_remote_update_settings_labels(app);
        ha_remote_set_header(
            app, app->vibration_enabled ? "Vibration On" : "Vibration Off", true);
        ha_remote_settings_save(app);
    } else if(index == HA_REMOTE_SETTINGS_LOG_INDEX) {
        app->log_mode = (HaRemoteLogMode)((app->log_mode + 1) % HaRemoteLogModeCount);
        ha_remote_update_settings_labels(app);
        snprintf(
            app->status_header,
            sizeof(app->status_header),
            "Log: %s",
            ha_remote_log_mode_labels[app->log_mode]);
        ha_remote_set_header(app, app->status_header, true);
        ha_remote_settings_save(app);
    } else if(index == HA_REMOTE_SETTINGS_BLE_HINT_INDEX) {
        ha_remote_set_header(app, "Long OK uses BLE", true);
    }
}

static void ha_remote_about_callback(void* context, InputType input_type, uint32_t index) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(input_type == InputTypeShort || input_type == InputTypeLong) {
        UNUSED(index);
        ha_remote_set_header(app, "Back for tabs", true);
    }
}

static void ha_remote_controller_callback(void* context, InputType input_type, uint32_t index) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(input_type != InputTypeShort && input_type != InputTypeLong) {
        return;
    }

    if(index == HA_REMOTE_REFRESH_INDEX) {
        ha_remote_drop_pending_command(app);
        if(app->action_order_count == 0) {
            ha_remote_set_header(app, "No rows yet", true);
            ha_remote_controller_model_sync(app, true);
            return;
        }
        ha_remote_start_controller_sync(app, true);
        return;
    }

    if(index < HA_REMOTE_ACTION_MENU_BASE) {
        return;
    }

    uint8_t entry_id;
    if(!ha_remote_controller_entry_id_for_item(app, (uint8_t)index, &entry_id)) {
        return;
    }

    if(ha_remote_entry_is_dimmer(app, entry_id)) {
        ha_remote_open_dimmer(app, entry_id);
        return;
    }

    if(input_type == InputTypeLong) {
        if(ha_remote_entry_is_builtin(entry_id)) {
            ha_remote_drop_pending_command(app);
            ha_remote_send_ble_action(app, &ha_remote_actions[entry_id]);
        } else {
            ha_remote_set_header(app, "WiFi Only", true);
        }
    } else {
        ha_remote_schedule_action_command(app, entry_id);
    }
}

static bool ha_remote_navigation_callback(void* context) {
    HaRemoteApp* app = context;
    furi_assert(app);

    if(app->current_view == HA_REMOTE_VIEW_CONTROLLER && app->controller_reorder_mode) {
        ha_remote_controller_exit_reorder(app);
        return true;
    }

    if(ha_remote_cancel_pending_command(app, "Command Canceled")) {
        return true;
    }

    if(app->current_view == HA_REMOTE_VIEW_DIMMER) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_controller_model_sync(app, true);
        return true;
    }

    if(app->current_view == HA_REMOTE_VIEW_CONTROLLER_INFO) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
        ha_remote_controller_model_sync(app, true);
        return true;
    }

    if(app->current_view == HA_REMOTE_VIEW_ADD_TEXT) {
        if(app->rename_active) {
            app->rename_active = false;
            ha_remote_switch_to_view(app, HA_REMOTE_VIEW_CONTROLLER);
            ha_remote_controller_model_sync(app, true);
            return true;
        }
        app->add_mode = HaRemoteAddModeConfirm;
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ADD);
        ha_remote_add_model_sync(app, true);
        return true;
    }

    if(app->current_view == HA_REMOTE_VIEW_ADD) {
        return ha_remote_add_go_back(app);
    }

    if(app->current_view != HA_REMOTE_VIEW_ROOT) {
        ha_remote_switch_to_view(app, HA_REMOTE_VIEW_ROOT);
        return true;
    }

    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static void ha_remote_stop_command_worker(HaRemoteApp* app) {
    if(!app->command_worker_thread) {
        return;
    }

    if(app->command_worker_started && app->command_queue) {
        HaRemoteQueuedCommand stop_command = {
            .type = HaRemotePendingCommandStop,
        };
        furi_message_queue_reset(app->command_queue);
        furi_message_queue_put(app->command_queue, &stop_command, FuriWaitForever);
        furi_thread_join(app->command_worker_thread);
    }

    furi_thread_free(app->command_worker_thread);
    app->command_worker_thread = NULL;
    app->command_worker_busy = false;
    app->command_worker_started = false;
}

static void ha_remote_free_partial(HaRemoteApp* app) {
    if(!app) {
        return;
    }

    furi_hal_bt_extra_beacon_stop();
    ha_remote_stop_command_worker(app);
    ha_remote_join_controller_sync(app);
    ha_remote_join_add_load(app);
    ha_remote_uart_close(app);

    if(app->uart_stream) {
        furi_stream_buffer_free(app->uart_stream);
    }

    if(app->beacon_timer) {
        furi_timer_stop(app->beacon_timer);
        furi_timer_free(app->beacon_timer);
    }

    if(app->status_timer) {
        furi_timer_stop(app->status_timer);
        furi_timer_free(app->status_timer);
    }

    if(app->command_timer) {
        furi_timer_stop(app->command_timer);
        furi_timer_free(app->command_timer);
    }

    if(app->marquee_timer) {
        furi_timer_stop(app->marquee_timer);
        furi_timer_free(app->marquee_timer);
    }

    if(app->command_queue) {
        furi_message_queue_free(app->command_queue);
    }

    if(app->command_result_queue) {
        furi_message_queue_free(app->command_result_queue);
    }

    if(app->bridge_mutex) {
        furi_mutex_free(app->bridge_mutex);
    }

    if(app->view_dispatcher && app->views_added) {
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_ROOT);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_CONTROLLER);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_THERMOSTAT);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_DIMMER);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_CONTROLLER_INFO);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_ADD);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_ADD_TEXT);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_SETTINGS);
        view_dispatcher_remove_view(app->view_dispatcher, HA_REMOTE_VIEW_ABOUT);
    }

    if(app->root_view) {
        view_free(app->root_view);
    }

    if(app->controller_view) {
        view_free(app->controller_view);
    }

    if(app->thermostat_view) {
        view_free(app->thermostat_view);
    }

    if(app->dimmer_view) {
        view_free(app->dimmer_view);
    }

    if(app->controller_info_view) {
        view_free(app->controller_info_view);
    }

    if(app->add_text_input) {
        text_input_free(app->add_text_input);
    }

    if(app->add_view) {
        view_free(app->add_view);
    }

    if(app->settings_menu) {
        submenu_free(app->settings_menu);
    }

    if(app->about_menu) {
        submenu_free(app->about_menu);
    }

    if(app->view_dispatcher) {
        view_dispatcher_free(app->view_dispatcher);
    }

    if(app->notifications) {
        furi_record_close(RECORD_NOTIFICATION);
    }

    free(app);
}

static HaRemoteApp* ha_remote_alloc(void) {
    HaRemoteApp* app = malloc(sizeof(HaRemoteApp));
    if(!app) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate app");
        return NULL;
    }

    memset(app, 0, sizeof(HaRemoteApp));
    app->vibration_enabled = true;
    app->log_mode = HaRemoteLogModeLight;
    app->current_view = HA_REMOTE_VIEW_ROOT;
    ha_remote_init_thermostat_state(app);
    ha_remote_bridge_config_load(app);
    ha_remote_thermostat_config_load(app);
    ha_remote_settings_load(app);
    ha_remote_controller_order_load(app);

    if(!ha_remote_init_beacon_payload(app)) {
        ha_remote_free_partial(app);
        return NULL;
    }

    app->beacon_timer =
        furi_timer_alloc(ha_remote_beacon_timer_callback, FuriTimerTypeOnce, app);
    if(!app->beacon_timer) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate beacon timer");
        ha_remote_free_partial(app);
        return NULL;
    }

    app->status_timer =
        furi_timer_alloc(ha_remote_status_timer_callback, FuriTimerTypeOnce, app);
    if(!app->status_timer) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate status timer");
        ha_remote_free_partial(app);
        return NULL;
    }

    app->command_timer =
        furi_timer_alloc(ha_remote_command_timer_callback, FuriTimerTypeOnce, app);
    app->marquee_timer =
        furi_timer_alloc(ha_remote_marquee_timer_callback, FuriTimerTypePeriodic, app);
    if(!app->command_timer || !app->marquee_timer) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate timers");
        ha_remote_free_partial(app);
        return NULL;
    }

    app->bridge_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->command_queue =
        furi_message_queue_alloc(HA_REMOTE_COMMAND_QUEUE_CAPACITY, sizeof(HaRemoteQueuedCommand));
    app->command_result_queue = furi_message_queue_alloc(
        HA_REMOTE_COMMAND_RESULT_QUEUE_CAPACITY, sizeof(HaRemoteCommandResult));
    app->command_worker_thread = furi_thread_alloc_ex(
        "HA Cmd Queue", HA_REMOTE_COMMAND_WORKER_STACK_SIZE, ha_remote_command_worker, app);
    if(!app->bridge_mutex || !app->command_queue || !app->command_result_queue ||
       !app->command_worker_thread) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate command queue");
        ha_remote_free_partial(app);
        return NULL;
    }
    furi_thread_start(app->command_worker_thread);
    app->command_worker_started = true;

    furi_hal_bt_extra_beacon_stop();

    GapExtraBeaconConfig config = {
        .min_adv_interval_ms = 100,
        .max_adv_interval_ms = 200,
        .adv_channel_map = GapAdvChannelMapAll,
        .adv_power_level = GapAdvPowerLevel_6dBm,
        .address_type = GapAddressTypePublic,
    };

    memcpy(config.address, furi_hal_version_get_ble_mac(), sizeof(config.address));

    if(!furi_hal_bt_extra_beacon_set_config(&config)) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to set beacon config");
        ha_remote_free_partial(app);
        return NULL;
    }

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->root_view = view_alloc();
    app->controller_view = view_alloc();
    app->thermostat_view = view_alloc();
    app->dimmer_view = view_alloc();
    app->controller_info_view = view_alloc();
    app->add_view = view_alloc();
    app->add_text_input = text_input_alloc();
    app->settings_menu = submenu_alloc();
    app->about_menu = submenu_alloc();

    if(!app->notifications || !app->view_dispatcher || !app->root_view ||
       !app->controller_view || !app->thermostat_view || !app->dimmer_view ||
       !app->controller_info_view || !app->add_view || !app->add_text_input ||
       !app->settings_menu || !app->about_menu) {
        FURI_LOG_E(HA_REMOTE_LOG_TAG, "Failed to allocate UI resources");
        ha_remote_free_partial(app);
        return NULL;
    }

    view_allocate_model(app->root_view, ViewModelTypeLocking, sizeof(HaRemoteRootModel));
    view_set_context(app->root_view, app);
    view_set_draw_callback(app->root_view, ha_remote_root_draw_callback);
    view_set_input_callback(app->root_view, ha_remote_root_input_callback);

    view_allocate_model(
        app->controller_view, ViewModelTypeLocking, sizeof(HaRemoteControllerModel));
    view_set_context(app->controller_view, app);
    view_set_draw_callback(app->controller_view, ha_remote_controller_draw_callback);
    view_set_input_callback(app->controller_view, ha_remote_controller_input_callback);
    ha_remote_controller_model_sync(app, false);

    view_allocate_model(
        app->thermostat_view, ViewModelTypeLocking, sizeof(HaRemoteThermostatModel));
    view_set_context(app->thermostat_view, app);
    view_set_draw_callback(app->thermostat_view, ha_remote_thermostat_draw_callback);
    view_set_input_callback(app->thermostat_view, ha_remote_thermostat_input_callback);
    ha_remote_thermostat_model_init(app);

    view_allocate_model(app->dimmer_view, ViewModelTypeLocking, sizeof(HaRemoteDimmerModel));
    view_set_context(app->dimmer_view, app);
    view_set_draw_callback(app->dimmer_view, ha_remote_dimmer_draw_callback);
    view_set_input_callback(app->dimmer_view, ha_remote_dimmer_input_callback);

    view_allocate_model(
        app->controller_info_view, ViewModelTypeLocking, sizeof(HaRemoteControllerInfoModel));
    view_set_context(app->controller_info_view, app);
    view_set_draw_callback(app->controller_info_view, ha_remote_controller_info_draw_callback);
    view_set_input_callback(app->controller_info_view, ha_remote_controller_info_input_callback);
    ha_remote_controller_info_model_sync(app, false);

    view_allocate_model(app->add_view, ViewModelTypeLocking, sizeof(HaRemoteAddModel));
    view_set_context(app->add_view, app);
    view_set_draw_callback(app->add_view, ha_remote_add_draw_callback);
    view_set_input_callback(app->add_view, ha_remote_add_input_callback);
    app->add_mode = HaRemoteAddModeCategories;
    app->add_load_ok = true;
    ha_remote_add_model_sync(app, false);

    ha_remote_set_header(app, "FlipperHA", false);

    submenu_set_header(app->settings_menu, "Settings");
    snprintf(
        app->vibration_label,
        sizeof(app->vibration_label),
        "Vibration [%s]",
        app->vibration_enabled ? "on" : "off");
    snprintf(
        app->log_mode_label,
        sizeof(app->log_mode_label),
        "Log [%s]",
        ha_remote_log_mode_labels[app->log_mode]);
    submenu_add_item_ex(
        app->settings_menu,
        app->vibration_label,
        HA_REMOTE_SETTINGS_VIBRATION_INDEX,
        ha_remote_settings_callback,
        app);
    submenu_add_item_ex(
        app->settings_menu,
        app->log_mode_label,
        HA_REMOTE_SETTINGS_LOG_INDEX,
        ha_remote_settings_callback,
        app);
    submenu_add_item_ex(
        app->settings_menu,
        "BLE Fallback: Long OK",
        HA_REMOTE_SETTINGS_BLE_HINT_INDEX,
        ha_remote_settings_callback,
        app);

    submenu_set_header(app->about_menu, "About");
    submenu_add_item_ex(
        app->about_menu,
        "FlipperHA v0.43",
        HA_REMOTE_ABOUT_VERSION_INDEX,
        ha_remote_about_callback,
        app);
    submenu_add_item_ex(
        app->about_menu,
        "Short OK: WiFi",
        HA_REMOTE_ABOUT_WIFI_INDEX,
        ha_remote_about_callback,
        app);
    submenu_add_item_ex(
        app->about_menu,
        "Long OK: BLE",
        HA_REMOTE_ABOUT_BLE_INDEX,
        ha_remote_about_callback,
        app);
    submenu_add_item_ex(
        app->about_menu,
        "States: FlipperHTTP",
        HA_REMOTE_ABOUT_STATE_INDEX,
        ha_remote_about_callback,
        app);
    submenu_add_item_ex(
        app->about_menu,
        "Thermostat: WiFi",
        HA_REMOTE_ABOUT_THERMO_INDEX,
        ha_remote_about_callback,
        app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, ha_remote_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ha_remote_navigation_callback);
    view_dispatcher_add_view(app->view_dispatcher, HA_REMOTE_VIEW_ROOT, app->root_view);
    view_dispatcher_add_view(
        app->view_dispatcher,
        HA_REMOTE_VIEW_CONTROLLER,
        app->controller_view);
    view_dispatcher_add_view(
        app->view_dispatcher,
        HA_REMOTE_VIEW_THERMOSTAT,
        app->thermostat_view);
    view_dispatcher_add_view(app->view_dispatcher, HA_REMOTE_VIEW_DIMMER, app->dimmer_view);
    view_dispatcher_add_view(
        app->view_dispatcher,
        HA_REMOTE_VIEW_CONTROLLER_INFO,
        app->controller_info_view);
    view_dispatcher_add_view(app->view_dispatcher, HA_REMOTE_VIEW_ADD, app->add_view);
    view_dispatcher_add_view(
        app->view_dispatcher, HA_REMOTE_VIEW_ADD_TEXT, text_input_get_view(app->add_text_input));
    view_dispatcher_add_view(
        app->view_dispatcher, HA_REMOTE_VIEW_SETTINGS, submenu_get_view(app->settings_menu));
    view_dispatcher_add_view(
        app->view_dispatcher, HA_REMOTE_VIEW_ABOUT, submenu_get_view(app->about_menu));
    app->views_added = true;

    return app;
}

int32_t ha_remote_app(void* p) {
    UNUSED(p);

    HaRemoteApp* app = ha_remote_alloc();
    if(!app) {
        return -1;
    }

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, HA_REMOTE_VIEW_ROOT);
    view_dispatcher_run(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    ha_remote_free_partial(app);
    return 0;
}
