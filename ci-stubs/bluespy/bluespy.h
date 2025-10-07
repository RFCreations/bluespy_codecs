#ifndef BLUESPY_INCLUDE_GUARD
#define BLUESPY_INCLUDE_GUARD

//!
//! @file bluespy.h
//! @brief blueSPY C API
//!

#ifndef BLUESPY_API
#if defined _WIN32 || defined __CYGWIN__
#define BLUESPY_API __declspec(dllimport)
#else
#define BLUESPY_API __attribute__((visibility("default")))
#endif
#endif

#include "stdbool.h"
#include "stdint.h"
#include <stddef.h>

#ifdef __cplusplus
#include <utility>
extern "C" {
#endif

#if defined(__has_cpp_attribute)
#define BLUESPY_HASATTR(attr) __has_cpp_attribute(attr)
#elif defined(__has_c_attribute)
#define BLUESPY_HASATTR(attr) __has_c_attribute(attr)
#else
#define BLUESPY_HASATTR(attr) (0)
#endif

#if BLUESPY_HASATTR(deprecated)
#define BLUESPY_DEPRECATED_API [[deprecated]] BLUESPY_API
#else
#define BLUESPY_DEPRECATED_API BLUESPY_API
#endif

typedef enum bluespy_error {
    BLUESPY_NO_ERROR = 0,
    BLUESPY_ERROR_NO_DEVICE,
    BLUESPY_ERROR_LICENCE,
    BLUESPY_ERROR_NO_FILE,
    BLUESPY_ERROR_CAPTURE_NOT_STARTED,
    BLUESPY_ERROR_INVALID_PACKET,
    BLUESPY_ERROR_INVALID_CAPTURE_OPTIONS,
    BLUESPY_ERROR_INVALID_CIS_CONFIG,
    BLUESPY_ERROR_AUDIOPOD_DISABLED,
    BLUESPY_ERROR_AUDIOPOD_OUTPUT_DISABLED,
    BLUESPY_ERROR_INVALID_PARAMETER,
    BLUESPY_ERROR_IUT_NOT_CONNECTED,
    BLUESPY_ERROR_INVALID_TCID,
    BLUESPY_ERROR_TESTCASE_FAILURE,
    BLUESPY_ERROR_ABORTED_TESTCASES,
    BLUESPY_ERROR_NOT_INITIALIZED,
} bluespy_error;

/**
 * @brief Get message for error
 * @param[in] error
 * @return Internal pointer to null terminated string (do not free)
 */
BLUESPY_API const char* bluespy_error_string(bluespy_error error);

/**
 * @brief Any value in [0, 255] can be used for logging.
 *
 * Colour-coding will be applied in the GUI based on the top three bits, and the bottom 5 bits
 * will be available as log.severity_level_subtype
 */
typedef enum bluespy_log_level {
    BLUESPY_LOG_PASS = 0x00,
    BLUESPY_LOG_WARN = 0x20,
    BLUESPY_LOG_INFO = 0x40,
    BLUESPY_LOG_DEBUG = 0x60,
    BLUESPY_LOG_ERROR = 0x80,

} bluespy_log_level;
/**
 * @brief Initialise bluespy, run once at start of program
 */
BLUESPY_API void bluespy_init();

/**
 * @brief Clean up before program exits
 */
BLUESPY_API void bluespy_deinit();

/**
 * @brief Start a GUI instance
 *
 * The GUI exists in a background thread, this function returns immediately
 */
BLUESPY_API void bluespy_start_gui();

/**
 * @brief Connect to Moreph
 * @param[in] serial - Serial number of the Moreph
 * @return Error code
 *
 * Connect by serial number, or first found on USB if serial == -1
 */
BLUESPY_API bluespy_error bluespy_connect(uint32_t serial);

/**
 * @brief Connect to Moreph in blueQ mode
 * @param[in] serial - Serial number of the Moreph
 * @return Error code
 *
 * Connect by serial number, or first found on USB if serial == -1
 */
BLUESPY_API bluespy_error blueQ_connect(uint32_t serial);

/**
 * @brief Connect to multiple Morephs
 * @param[in] serial_data - Serial numbers of the Morephs
 * @param[in] serial_size - Number of Morephs to connect
 * @return Error code
 *
 * Connect by serial numbers
 */
BLUESPY_API bluespy_error bluespy_connect_multiple(uint32_t* serial_data, uint64_t serial_size);

/**
 * @brief Lists the serial numbers of the connected morephs
 * @param[out] serials - Pointer to array of serial numbers
 * @return Returns the number of connected morephs
 */
BLUESPY_API uint64_t bluespy_morephs_connected(uint32_t** serials);

/**
 * @brief Disconnect from Morephs
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_disconnect();

/**
 * @brief Reboot Moreph
 * @param[in] serial - Serial number of the Moreph
 * @return Error code
 *
 * Reboot by serial number, or first found on if serial == -1
 * This function will cause the specified Moreph to disconnect - bluespy_connect needs to be called
 * afterwards to talk to the Moreph again
 */
BLUESPY_API bluespy_error bluespy_moreph_reboot(uint32_t serial);

/**
 * The time in nanoseconds since the epoch 1970/01/01 00:00 UTC
 * BLUESPY_TIME_INVALID (0x7fff'ffff'ffff'ffff) represents an invalid time point
 */
typedef int64_t bluespy_time_point;
#define BLUESPY_TIME_INVALID (INT64_MAX)

/**
 * @brief Prints the time in a hh:mm:ss.xxx format
 * @param[in] ts
 * @return Time string
 */
BLUESPY_API const char* bluespy_print_time(bluespy_time_point ts);

/**
 * @brief Adds a log message into the file. Pass BLUESPY_TIME_INVALID to log at the present time.
 * @param[in] level
 * @param[in] message
 * @param[in] ts
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_add_log_message(bluespy_log_level level, const char* message,
                                                  bluespy_time_point ts);

typedef enum bluespy_logic_rate {
    bluespy_logic_rate_high,
    bluespy_logic_rate_mid,
    bluespy_logic_rate_low

} bluespy_logic_rate;

typedef struct bluespy_multi_moreph_options {
    bool enable_CL;
    bool enable_LE;
    bool enable_wifi;
} bluespy_multi_moreph_options;
typedef enum bluespy_audio_channel {
    BLUESPY_STEREO,
    BLUESPY_MONO_L,
    BLUESPY_MONO_R
} bluespy_audio_channel;

typedef enum bluespy_audio_connect {
    BLUESPY_NOAUDIO,
    BLUESPY_LINE,
    BLUESPY_JACK,
    BLUESPY_HEADSET,
    BLUESPY_COAX,
    BLUESPY_OPTICAL,
    BLUESPY_MIC,
    BLUESPY_I2S
} bluespy_audio_connect;

typedef enum bluespy_audio_bias {
    BLUESPY_BIAS_OFF,
    BLUESPY_BIAS_LOW,
    BLUESPY_BIAS_MID,
    BLUESPY_BIAS_HIG,
    BLUESPY_BIAS_VDD
} bluespy_audio_bias;

typedef struct bluespy_capture_audiopod_options {
    /**
     * Valid sample rates in Hz are:
     * 8'000, 11'025, 16'000, 22'050, 32'000, 44'100, 48'000, 88'200, 96'000, 176'400, 192'000
     */
    uint32_t sample_rate;
    bluespy_audio_channel channels;
    bluespy_audio_connect output;
    bluespy_audio_connect input;
    bluespy_audio_bias bias;
    bool current_probe;
    /**
     * LA_low_voltage and LA_high_voltage must be within the range of 0.0 to 3.3
     */
    double LA_low_voltage;
    double LA_high_voltage;
    /**
     * power_supply_V must be within the range of 0.6 to 5.0
     */
    double power_supply_V;
    double VIO_dV;
    bool AGC;
    bool DRC;
    bool second_I2S_input; // Enable a second I2S input, output must be set to BLUESPY_NO_AUDIO
    double vol_in_left;
    double vol_in_right;
    double vol_out_left;
    double vol_out_right;
} bluespy_capture_audiopod_options;

typedef struct bluespy_capture_i2s_options {
    uint8_t SCLK_line;   // Valid values are [0,15]
    uint8_t WS_line;     // Valid values are [0,15]
    uint8_t SD_line;     // Valid values are [0,15]
    uint8_t n_channels;  // Valid values are [1,16]
    uint8_t bits_per_ch; // Valid values are [1,32]
    bool sample_on_rising_edge;
    bool first_chan_follows_WS_high;
    bool one_sample_delay;
    bool MSB_first;
    bool DSP_mode;
    bool master;
} bluespy_capture_i2s_options;

typedef struct bluespy_capture_options {
    bool enable_CL;
    bool enable_LE;
    bool enable_QHS;
    bool enable_15_4;
    bool enable_wifi;
    bool enable_MHDT_CL;
    bool enable_MHDT_LE;
    bool enable_Dukosi;
    bool enable_Varjo;
    bool enable_Channel_Sounding;
    bool enable_HDT;
    /**
     * Valid spectrum periods in microseconds are:
     * 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000
     *
     * 0 to disable spectrum capture.
     */
    uint16_t spectrum_period;

    uint32_t logic_mask;
    bool logic_use_external_vref;
    bluespy_logic_rate logic_rate;
    /**
     * If multiple Morephs are connected, capture options specific to each device after the first
     * (LE, CL, WiFi).
     */
    bluespy_multi_moreph_options multi_moreph_opts[32];
    bluespy_capture_audiopod_options* audiopod_opts;
    bluespy_capture_i2s_options* i2s_opts[2];

    bool enable_Proprietary_1;
    bool enable_Proprietary_2;
} bluespy_capture_options;

/**
 * @brief Create a bluespy_capture_options struct
 * @return New struct
 */
BLUESPY_API bluespy_capture_options* bluespy_capture_options_alloc();

/**
 * @brief Delete a bluespy_capture_options struct
 */
BLUESPY_DEPRECATED_API void bluespy_capture_options_delete(bluespy_capture_options* opts);

/**
 * @brief Delete any of the following bluespy structs:
 * * bluespy_capture_options
 * * bluespy_capture_audiopod_options
 * * bluespy_capture_i2s_options
 * @param[in] obj - Object to delete
 */
BLUESPY_API void bluespy_delete(void* obj);

/**
 * @brief Create a bluespy_capture_audiopod_options struct
 * @return New struct
 */
BLUESPY_API bluespy_capture_audiopod_options* bluespy_capture_audiopod_options_alloc();

/**
 * @brief Create a bluespy_capture_i2s_options struct
 * @return New struct
 */
BLUESPY_API bluespy_capture_i2s_options* bluespy_capture_i2s_options_alloc();

/**
 * @brief Start a capture
 * @param[in] filename - UTF8 filename
 * @param[in] opts - Capture options
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_capture(const char* filename, bluespy_capture_options* opts);

typedef enum blueQ_serial_flow_control {
    BLUEQ_SERIAL_FLOW_CONTROL_NONE,
    BLUEQ_SERIAL_FLOW_CONTROL_SOFTWARE,
    BLUEQ_SERIAL_FLOW_CONTROL_HARDWARE,
} blueQ_serial_flow_control;

typedef enum blueQ_serial_parity_bits {
    BLUEQ_SERIAL_PARITY_BITS_NONE,
    BLUEQ_SERIAL_PARITY_BITS_ODD,
    BLUEQ_SERIAL_PARITY_BITS_EVEN,
} blueQ_serial_parity_bits;

typedef enum blueQ_serial_stop_bits {
    BLUEQ_SERIAL_STOP_BITS_ONE,
    BLUEQ_SERIAL_STOP_BITS_ONE_POINT_FIVE,
    BLUEQ_SERIAL_STOP_BITS_TWO,
} blueQ_serial_stop_bits;

/**
 * @brief Connect to an IUT over a serial port
 * @param[in] port - Serial port
 * @param[in] rate - Baud-rate
 * @param[in] flow_control - Flow control: HW, SW, or none.
 * @param[in] parity_bits - Number of parity bits
 * @param[in] stop_bits - Stop-bit length.
 * @return Error code
 */
BLUESPY_API bluespy_error blueQ_connect_IUT_serial(const char* port, uint32_t rate,
                                                   blueQ_serial_flow_control flow_control,
                                                   blueQ_serial_parity_bits parity_bits,
                                                   blueQ_serial_stop_bits stop_bits);

/**
 * @brief Specify configuration files for blueQ
 * @param[in] IXIT_file - Filepath to an IXIT file
 * @param[in] ICS_file - Filepath to an ICS file, not yet required
 * @param[in] options - Further options for blueQ, not yet used.
 * @return Error code
 */
BLUESPY_API bluespy_error blueQ_set_config(const char* IXIT_file, const char* ICS_file,
                                           const void* options);

typedef enum blueQ_testcase_verdict {
    BLUEQ_VERDICT_PASSED,
    BLUEQ_VERDICT_FAILED,
    BLUEQ_VERDICT_INCONCLUSIVE,
    BLUEQ_VERDICT_INTERNAL_BLUEQ_ERROR,
    BLUEQ_VERDICT_INITIAL_CONDITION_NOT_ESTABLISHED,
    BLUEQ_VERDICT_TESTCASE_IS_INVALID,
} blueQ_testcase_verdict;

/**
 * @brief Get message for testcase verdict
 * @param[in] verdict
 * @return Internal pointer to null terminated string (do not free)
 */
BLUESPY_API const char* blueQ_testcase_verdict_string(blueQ_testcase_verdict verdict);

typedef struct blueQ_result_data {
    int64_t start_ts;
    int64_t end_ts;
    bluespy_error error;
    blueQ_testcase_verdict verdict;
} blueQ_result_data;

typedef enum blueQ_verbosity {
    BLUEQ_VERBOSITY_NONE = 0,
    BLUEQ_VERBOSITY_TESTCASES = 0x10,
    BLUEQ_VERBOSITY_DETAILS = 0x20,
} blueQ_verbosity;

/**
 * @brief Get message for verbosity
 * @param[in] verdict
 * @return Internal pointer to null terminated string (do not free)
 */
BLUESPY_API const char* blueQ_verbosity_string(blueQ_verbosity verbosity);

/**
 * @brief Run a single blueQ testcase. NB: Blocks until test completes or fails
 * @param[in] TCID - Testcase ID, in format from specification. E.g. "LL/CS/CEN/BI-01-C"
 * @param[in] print_progress - Enable printing of test-step progress to stdout
 * @return Results, with bluespy_error if test failed to run, or blueQ_testcase_verdict otherwise
 */
BLUESPY_API blueQ_result_data blueQ_run_test(const char* TCID, blueQ_verbosity print_verbosity);

/**
 * @brief Stop a capture
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_stop_capture();

/**
 * @brief Load a capture
 * @param[in] filename - UTF8 filename
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_load_file(const char* filename);

/**
 * @brief Close current file
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_close_file();

/**
 * @brief Number of baseband packets loaded
 * @return N
 */
BLUESPY_API uint32_t bluespy_packet_count(void);

/**
 * @brief General Identifier for an event, device, connection, or audiostream (queryable)
 *
 * BLUESPY_ID_INVALID means invalid.
 * Only use bluespy_ids returned by the API.
 * Functions accepting a bluespy_id can accept any bluespy_???_id.
 * After bluespy_load_file or bluespy_capture all previous ids are invalid.
 */
typedef uint64_t bluespy_id;

/**
 * @brief Invalid value, used to represent N/A.
 */
#define BLUESPY_ID_INVALID ((bluespy_id)(-1))

/**
 * @brief Identifier for an packet or higher layer event
 *
 * BLUESPY_ID_INVALID means invalid.
 * Only use bluespy_event_ids returned by the API.
 * After bluespy_load_file or bluespy_capture all previous ids are invalid.
 */
typedef bluespy_id bluespy_event_id;

/**
 * @brief Identifier for a device
 *
 * BLUESPY_ID_INVALID means invalid.
 * Device IDs are ordered arbitrarily.
 * Only use bluespy_device_ids returned by the API.
 * After bluespy_load_file or bluespy_capture all previous ids are invalid.
 */
typedef bluespy_id bluespy_device_id;

/**
 * @brief Identifier for a connection
 *
 * BLUESPY_ID_INVALID means invalid.
 * Connection IDs are ordered arbitrarily.
 * Only use bluespy_connection_ids returned by the API.
 * After bluespy_load_file or bluespy_capture all previous ids are invalid.
 */
typedef bluespy_id bluespy_connection_id;

/**
 * @brief Identifier for an audio stream
 *
 * BLUESPY_ID_INVALID means invalid.
 * Audio stream IDs are ordered arbitrarily.
 * Only use bluespy_audiostream_ids returned by the API.
 * After bluespy_load_file or bluespy_capture all previous ids are invalid.
 */
typedef bluespy_id bluespy_audiostream_id;

/**
 * @brief Get a baseband packet
 * @param[in] index - 0 <= index < bluespy_packet_count()
 * @return Event ID
 */
BLUESPY_API bluespy_event_id bluespy_get_baseband(uint32_t index);

/**
 * @brief Get higher layer packets
 * @param[in] event
 * @return Event ID
 */
BLUESPY_API bluespy_event_id bluespy_get_parent(bluespy_event_id event);

/**
 * @brief Get all lower layer packets
 * @param[in] event
 * @param[out] count - size of returned array
 * @return Event ID array
 *
 * Returned child array is only valid until next call, so take a copy
 * Returned child array is null-terminated with BLUESPY_ID_INVALID
 */
BLUESPY_API const bluespy_event_id* bluespy_get_children(bluespy_event_id event, uint32_t* count);

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @return String result of a query
 *
 * Returned value is valid until next call, so take a copy
 */
BLUESPY_API const char* bluespy_query(bluespy_id event, const char* query);

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @return Integer result of a query
 */
BLUESPY_API int64_t bluespy_query_int(bluespy_id event, const char* query);

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @return Bool result of a query
 */
BLUESPY_API bool bluespy_query_bool(bluespy_id event, const char* query);

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @param[out] s - return if string
 * @param[out] i - return if int
 * @param[out] b - return if bool
 * @return 0 = None, 1 = str, 2 = int, 3 = bool
 *
 * Returned value is valid until next call, so take a copy
 *
 * Use bluespy_query_get instead
 */
BLUESPY_DEPRECATED_API int bluespy_query_auto(bluespy_id event, const char* query, const char** s,
                                              int64_t* i, bool* b);

typedef struct bluespy_bytes {
    const uint8_t* data;
    size_t len;
} bluespy_bytes;

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @return Bytes result of a query
 */
BLUESPY_API bluespy_bytes bluespy_query_bytes(bluespy_id event, const char* query);

typedef enum bluespy_query_type {
    bluespy_query_type_invalid = -1, /* This query can never be used on this kind of ID */
    bluespy_query_type_none = 0,     /* There is no value on this specific ID */
    bluespy_query_type_bool = 1,
    bluespy_query_type_int = 2,
    bluespy_query_type_string = 3,
    bluespy_query_type_bytes = 4,
    bluespy_query_type_double = 5,
    bluespy_query_type_id = 6,
} bluespy_query_type;

typedef struct bluespy_query_value {
    bluespy_query_type type;
    union {
        bool b;
        int64_t i;
        const char* s;
        bluespy_bytes bytes;
        double d;
        bluespy_id id;
    };
} bluespy_query_value;

/**
 * @brief Query an event, device, connection, or audio stream.
 * @param[in] event
 * @param[in] query - query string to apply
 * @return bluespy_query_value
 *
 * Referenced str/bytes data is valid until next call, so take a copy
 */
BLUESPY_API bluespy_query_value bluespy_query_get(bluespy_id id, const char* query);

/**
 * @brief Indentifier for a capture file stored by the API itself.
 * -1 means invalid
 * Only use bluespy_filter_file_ids returned by the API
 * After bluespy_load_file or bluespy_capture all previous ids are invalid
 */
typedef int32_t bluespy_filter_file_id;
#define BLUESPY_FILTER_FILE_ID_INVALID ((bluespy_filter_file_id)(-1))

typedef struct bluespy_filter_file_options {
    bluespy_time_point range_start;
    bool keep_spectrum;
    bool keep_logic;
    bool keep_uart;
    bool keep_i2s_and_audiopod;
} bluespy_filter_file_options;

/**
 * @brief Create a bluespy_filter_file_options struct
 * @return New struct
 */
BLUESPY_API bluespy_filter_file_options* bluespy_filter_file_options_alloc();

/**
 * @brief Delete a bluespy_filter_file_options struct
 */
BLUESPY_API void bluespy_filter_file_options_delete(bluespy_filter_file_options* opts);

/**
 * @brief Get file name from ID
 * @param[in] id - File ID
 * @return file name
 */
BLUESPY_API const char* bluespy_get_filter_file_name(bluespy_filter_file_id id);

/**
 * @brief Create a capture file
 * @param[in] filename - File name
 * @param[in] opts - Filter File options
 * @return ID of file
 */
BLUESPY_API bluespy_filter_file_id bluespy_create_filter_file(const char* filename,
                                                              bluespy_filter_file_options* opts);

/**
 * @brief Adds an event to a capture file
 * @param[in] file_id - File ID
 * @param[in] event_id - Event ID
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_add_to_filter_file(bluespy_filter_file_id file_id,
                                                     bluespy_event_id event_id);

/**
 * @brief Close a capture file
 * @param[in] file_id - File ID
 * @return Error code
 */
BLUESPY_API bluespy_error bluespy_close_filter_file(bluespy_filter_file_id file_id);

/**
 * @brief Get the logic state at a given time
 * @param[in] ts - timestamp
 * @return Logic state mask
 */
BLUESPY_API uint32_t bluespy_get_logic_at_time(bluespy_time_point ts);

typedef struct bluespy_logic_change {
    uint32_t state;          // New logic state - 32-bit mask of the new logic state
    uint32_t change_mask;    // Logic changed - 32-bit mask of lines which changed
    bluespy_time_point time; // The time of the logic change
} bluespy_logic_change;

/**
 * @brief Get the next logic state after a given time
 * @param[in] ts - timestamp
 * @param[in] mask - logic mask for which lines to look at
 * @return Logic state mask and time of the next change
 */
BLUESPY_API bluespy_logic_change bluespy_get_next_logic_change(bluespy_time_point ts,
                                                               uint32_t mask);

/**
 * @brief Wait until any of the logic lines within the mask changes, and returns the result
 * @param[in] mask - logic mask for which lines to look at
 * @param[in] timeout - maximum time to wait in ns
 * @param[in] start_ts - time to wait from. -1 = wait from present
 * @return Struct containing the new logic state, the logic lines which changed, and the time the
 * change occured. Timeout - returns {0,0,current_time}
 */
BLUESPY_API bluespy_logic_change bluespy_wait_until_next_logic_change(uint32_t mask,
                                                                      int64_t timeout,
                                                                      bluespy_time_point start_ts);

/**
 * @brief Add a link key for decryption
 * @param[in] key - 16 bytes of binary
 * @param[in] addr0 - Bluetooth address of first device, set to 0 if unknown
 * @param[in] addr1 - Bluetooth address of second device, set to 0 if unknown
 * @return Error code
 *
 * The link key should be added before loading/capturing, or you should load again afterwards.
 */
BLUESPY_API bluespy_error bluespy_add_link_key(const unsigned char* key, uint64_t addr0,
                                               uint64_t addr1);

/**
 * @brief Add an IRK
 * @param[in] key - 16 bytes of binary
 * @param[in] addr - Array of relevant Bluetooth addresses, can be empty. Set bit 48 == 1 for
 * Random, == 0 for Public
 * @param[in] n_addresses - Length of array
 *
 * The key should be added before loading/capturing, or you should load again afterwards.
 */
BLUESPY_API void bluespy_add_IRK(const unsigned char* key, uint64_t* addr, uint64_t n_addresses);

/**
 * @brief Obtains the Device ID from an address
 * @param[in] addr - 6 bytes representing the address
 * @return Device ID. BLUESPY_ID_INVALID = Unknown or Invalid address
 */
BLUESPY_API bluespy_device_id bluespy_get_device_id(const char* addr);

typedef struct bluespy_connection_id_span {
    bluespy_connection_id* data;
    uint64_t size;
} bluespy_connection_id_span;

/**
 * @brief Obtains the IDs of the connections which a device is creating
 * @param[in] dev_id - Device ID
 * @return An array of Connection IDs
 */
BLUESPY_API bluespy_connection_id_span bluespy_get_connections(bluespy_device_id dev_id);

typedef struct bluespy_audiostream_id_span {
    bluespy_audiostream_id* data;
    uint64_t size;
} bluespy_audiostream_id_span;

/**
 * @brief Obtains the IDs of the audio streams which a device is creating or which are part of a
 * connection.
 * @param[in] id - Bluespy ID
 * @return An array of audio stream IDs
 *
 * If id is device_id: Streams which a device is creating.\n
 * If id is connection_id: Streams which are part of a connection.\n
 * If id is BLUESPY_ID_INVALID: Streams not associated with any device, e.g. audiopod streams.
 */
BLUESPY_API bluespy_audiostream_id_span bluespy_get_audiostreams(bluespy_id id);

/**
 * @brief Get the next Device ID after 'id'. Used to iterate over all devices.
 * @param[in] id - Device ID
 * @return Device ID
 *
 * id = BLUESPY_ID_INVALID returns the first device id.
 * Returns BLUESPY_ID_INVALID if there are no devices remaining.
 */
BLUESPY_API bluespy_device_id bluespy_get_next_device_id(bluespy_device_id id);

/**
 * @brief Get the first out of all available Device IDs. Used to iterate over all devices.
 * @return Device ID
 *
 */
inline bluespy_device_id bluespy_get_first_device_id() {
    return bluespy_get_next_device_id(BLUESPY_ID_INVALID);
}

/**
 * @brief Get the next Connection ID after 'id'. Used to iterate over all connections.
 * @param[in] id - Connection ID
 * @return Connection ID
 *
 * id = BLUESPY_ID_INVALID returns the first connection id.
 * Returns BLUESPY_ID_INVALID if there are no connections remaining.
 */
BLUESPY_API bluespy_connection_id bluespy_get_next_connection_id(bluespy_connection_id id);

/**
 * @brief Get the first out of all available Connection IDs. Used to iterate over all connections.
 * @return Connection ID
 */
inline bluespy_connection_id bluespy_get_first_connection_id() {
    return bluespy_get_next_connection_id(BLUESPY_ID_INVALID);
}

/**
 * @brief Get the next audio stream ID after 'id'. Used to iterate over all audio streams.
 * @param[in] id - Audio stream ID
 * @return Audio stream ID
 *
 * id = BLUESPY_ID_INVALID returns the first audio stream id.
 * Returns BLUESPY_ID_INVALID if there are no audio streams remaining.
 */
BLUESPY_API bluespy_audiostream_id bluespy_get_next_audiostream_id(bluespy_audiostream_id id);

/**
 * @brief Get the first out of all available audio stream IDs. Used to iterate over all audio
 * streams.
 * @return Audio stream ID
 *
 */
inline bluespy_audiostream_id bluespy_get_first_audiostream_id() {
    return bluespy_get_next_audiostream_id(BLUESPY_ID_INVALID);
}

typedef enum bluespy_event_types {
    bluespy_event_bt_baseband,
    bluespy_event_custom,
    bluespy_event_proprietary_1,
    bluespy_event_proprietary_2,
} bluespy_event_types;
/**
 * @brief Sets the callback for bluespy events
 * @param[in] types - The types of events that the callback should be called for
 * @param[in] callback - The callback function
 *
 * Use this function to add a callback for processing custom protocols and other propietary features
 */
BLUESPY_API void bluespy_register_event_callback(bluespy_event_types types,
                                                 void (*callback)(bluespy_event_id));

typedef void (*bluespy_cleanup_t)(void*);
/**
 * @brief Allocates memory that bluespy will automatically clean up
 * @param[in] bytes - The number of bytes to allocate
 * @param[in] cleanup - Optional function to be called when the allocation is later deallocated.
 * Usually NULL
 *
 * This function should be primarily used in custom decoders to allocate memory for new events
 */
BLUESPY_API void* bluespy_allocate(size_t bytes, bluespy_cleanup_t cleanup);

typedef struct bluespy_custom_event {
    bluespy_event_id* children;
    unsigned int n_children;
    bluespy_query_value (*query)(const struct bluespy_custom_event* self, const char* query_str,
                                 bool prefer_string);
} bluespy_custom_event;

/**
 * @brief Adds a custom event to the bluespy event log
 *
 * Used to add custom events for things like custom protocols
 */
BLUESPY_API bluespy_error bluespy_add_event(bluespy_custom_event* event);

typedef enum bluespy_latency_status {
    BLUESPY_OK,
    BLUESPY_ZEROS,
    BLUESPY_NOT_ENOUGH_DATA,
    BLUESPY_ENERGY_THRESHOLD,
    BLUESPY_AMBIG_PEAK,
    BLUESPY_PERIODIC,
    BLUESPY_OTHER_ERROR
} bluespy_latency_status;

/**
 * @brief Get message for latency status
 * @param[in] status
 * @return Internal pointer to null terminated string (do not free)
 */
BLUESPY_API const char* bluespy_latency_status_string(bluespy_latency_status status);

typedef struct bluespy_latency_result {
    int64_t time_difference_ns;
    int64_t time_difference_min_ns;
    int64_t time_difference_max_ns;
    bluespy_time_point measurement_time;
    bluespy_latency_status status;
    double total_energy;
    double peak_ratio;
    bool three_measurements_expected;
} bluespy_latency_result;

typedef struct bluespy_audio_channel_t {
    bluespy_audiostream_id ID;
    uint8_t channel_index;
} bluespy_audio_channel_t;

/**
 * @brief Measure the latency between two audio streams
 * @param[in] channel0 - Audio Channel
 * @param[in] channel1 - Audio Channel
 * @param[in] include_pres_delay - Whether to include presentation delay
 * @param[in] ts - Time to measure at
 * @return latency result
 */
BLUESPY_API bluespy_latency_result bluespy_measure_latency(bluespy_audio_channel_t channel0,
                                                           bluespy_audio_channel_t channel1,
                                                           bool include_pres_delay,
                                                           bluespy_time_point ts);

typedef struct bluespy_cis_lc3_config {
    uint64_t codec_frames_per_SDU;
    uint64_t presentation_delay_us;
    uint32_t octets_per_codec_frame;
    uint32_t frame_duration_us;
    uint32_t sampling_frequency_Hz;
    uint32_t audio_channel_allocation;
} bluespy_cis_lc3_config;

/**
 * @brief Set the configuration of a CIS stream
 * @param[in] id - Audio stream ID
 * @param[in] conf - Pointer to CIS config
 * @return error
 */
BLUESPY_API bluespy_error bluespy_set_cis_lc3_config(bluespy_audiostream_id id,
                                                     bluespy_cis_lc3_config* conf);
/**
 * @brief Play audio file to Audiopod File Playback
 * @param[in] filename - Audio file
 * @param[in] loop - Whether to loop the file
 * @return error
 */
BLUESPY_API bluespy_error bluespy_play_to_audiopod_output(const char* filename, bool loop);

/**
 * @brief Stop the audio file in Audiopod File Playback
 * @return error
 */
BLUESPY_API bluespy_error bluespy_stop_audio();

/**
 * @brief Mark an encryption key as used. Most useful in custom decoder where the custom decoder
 * performs decryption. This will highlight the key green in the GUI and cause it to be included in
 * the capture.
 */
BLUESPY_API bluespy_error bluespy_mark_key_used(const char* key, size_t len);

typedef struct bluespy_key {
    size_t length;
    uint8_t* key;
} bluespy_key;

/**
 * @brief List keys from the security tab. Later, call bluespy_free_keys
 */
BLUESPY_API bluespy_error bluespy_list_keys(bluespy_key** keys, size_t* count);

/**
 * @brief Free the keys returned by bluespy_list_keys
 */
BLUESPY_API bluespy_error bluespy_free_keys(bluespy_key* keys, size_t count);

/**
 * @brief Library-level information describing a codec implementation. 
 * 
 * Each codec shared library (e.g. AAC, aptX) must expose an 'init()' function returning
 * an instance of this structure. It identifies the codec library at runtime and provides
 * a library version for compatibility checking.
 */
typedef struct bluespy_audio_codec_lib_info {
    int api_version;
    const char* codec_name;
} bluespy_audio_codec_lib_info;

/**
 * @brief Function pointer type for codec library initialisation function.
 * 
 * Each codec library must export a symbol named 'init()' matching this signature.
 * It provides information about the library and its capabilities.
 * 
 * @return A structure containing the codec libray information.
 */
typedef bluespy_audio_codec_lib_info (*bluespy_audio_codec_lib_init_t)(void);

/**
 * @brief Enumeration of transport/container types used to deliver codec data.
 * 
 * This is used to differentiate between classic Bluetooth (AVDTP/A2DP) and 
 * Bluetooth LE Audio (LEA) transports.
 */
typedef enum bluespy_codec_container {
    BLUESPY_CODEC_AVDTP,
    BLUESPY_CODEC_LEA
} bluespy_codec_container;

/**
 * @brief Enumeration of supported codec identifiers.
 * 
 * This identifies the specific audio codec used in a stream.
 */
typedef enum bluespy_codec_id {
    BLUESPY_CODEC_AAC,
    BLUESPY_CODEC_APTX,
    BLUESPY_CODEC_APTX_HD,
    BLUESPY_CODEC_OTHER
} bluespy_codec_id;

/**
 * @brief Describes the codec configuration for a given audio stream.
 * 
 * This structure encapsulates codec-specific configuration blocks typically
 * obtained from Bluetooth signalling (A2DP or LEA). The 'container' field 
 * determines which member of the 'data' union is valid. 
 */
typedef struct bluespy_audio_codec_info {
    bluespy_codec_container container; // discriminant for union
    bluespy_codec_id type;
    union {
        struct {
            const uint8_t* AVDTP_Media_Codec_Specific_Information;
            uint32_t len;
        } AVDTP;
        struct {
            const uint8_t* ASE_Control_Point_Config_Codec;
            uint32_t len;
        } LEA;
    } data;
} bluespy_audio_codec_info;

/**
 * @brief Describes the decoded audio format produced by a codec.
 * 
 * Each codec must report its decoded sample format as part of its 
 * initialisation return structure.
 */
typedef struct bluespy_audio_codec_decoded_format {
    uint32_t sample_rate;
    uint8_t n_channels;
    uint8_t bits_per_sample;
} bluespy_audio_codec_decoded_format;

/**
 * @brief Represents a buffer of decoded PCM audio and optional metadata.
 * 
 * Returned by the codec 'decode()' function for each audio frame.
 */
typedef struct bluespy_audio_codec_decoded_audio {
    const uint8_t* data;
    uint32_t len;

    bool has_metadata;
    uint64_t source_id;
} bluespy_audio_codec_decoded_audio;

/**
 * @brief Function pointer type for the codec_decode function.
 * 
 * Called once per encoded packet/frame to produce decoded PCM audio data.
 * The decoder should maintain any necessary state to handle continuity or packet
 * loss concealment.
 * 
 * @param id Identifier for the active audio stream.
 * @param payload Pointer to the encoded audio packet or frame.
 * 
 * For Classic Bluetooth (A2DP/AVDTP), this buffer represents the contents of a single
 * L2CAP Service Data Unit (SDU) carrying an AVDTP media packet. This usually contains an
 * RTP header (12 bytes plus optional CSRC fields) followed by codec-specific frame data.
 * 
 * For Bluetooth LE Audio, the buffer represents the contents of a single ISOAL SDU,
 * containing one codec frame (e.g. LC3) as transmitted over the ISO isochronous channel.
 * 
 * The implementation should interpret this payload according to the transport 'container'
 * type provided in the corresponding bluespy_audio_codec_info structure. The library does
 * not further fragment or reassemble payloads - each call receives a complete SDU as 
 * received over the air.
 * 
 * @param payload_len Length of the encoded audio packet in bytes.
 * @param event_id Unique identifier for this L2CAP/ISOAL SDU (see payload parameter above) from the capture.
 *  
 * This allows the codec to return metadata that can be used to correlate
 * decoded audio with captured packets.
 * 
 * @return A structure containing decoded PCM audio data and any metadata.
 */
typedef bluespy_audio_codec_decoded_audio (*bluespy_audio_decode_t)(bluespy_audiostream_id id, 
                                                                    const uint8_t* payload,
                                                                    const uint32_t payload_len,
                                                                    int32_t event_id);

/**
 * @brief Function pointer type for codec_deinit function.
 * 
 * Called when an audio stream ends in order to free codec state and resources.
 * 
 * @param id Identifier for the audio stream that is to be deinitialised.
 */
typedef void (*bluespy_audio_codec_deinit_t)(bluespy_audiostream_id id);

/**
 * @brief Collection of function pointers exposed by a codec implementation.
 * 
 * Each initialised codec must provide a decode and deinit function via this structure.
 */
typedef struct bluespy_audio_codec_funcs {
    bluespy_audio_decode_t decode;
    bluespy_audio_codec_deinit_t deinit;
} bluespy_audio_codec_funcs;

/**
 * @brief return structure for the codec initialisation funciton.
 * 
 * Provides the initial decode format, function table, and any error information.
 */
typedef struct bluespy_audio_codec_init_ret {
    int error;
    bluespy_audio_codec_decoded_format format;
    bluespy_audio_codec_funcs fns;
} bluespy_audio_codec_init_ret;

/**
 * @brief Function pointer type for initialising a new codec instance.
 * 
 * This function is called when a Bluetooth audio stream is started. 
 * It should allocate and initialise codec-specific state and return 
 * decode capabilities and handlers.
 * 
 * @param id Identifier for the newly created audio stream.
 * @param infp Pointer to codec configuration information for this stream.
 * 
 * @return An initialisation result containing the decode format, function pointers, and error code. 
 */
typedef bluespy_audio_codec_init_ret (*bluespy_audio_codec_init_t)(bluespy_audiostream_id id, const bluespy_audio_codec_info* info);

#ifdef __cplusplus
}

namespace bluespy {
template <class T, class... Args>
T* allocate(Args&&... args) {
    T* p = static_cast<T*>(bluespy_allocate(sizeof(T), [](void* p) { ((T*)p)->~T(); }));
    new (p) T(std::forward<Args>(args)...);
    return p;
}

inline bluespy_error connect(uint32_t serial = -1) { return bluespy_connect(serial); }
} // namespace bluespy

namespace blueQ {
inline bluespy_error connect(uint32_t serial = -1) { return blueQ_connect(serial); }

inline bluespy_error
connect_IUT_serial(const char* port, uint32_t rate = 115200,
                   blueQ_serial_flow_control flow_control = BLUEQ_SERIAL_FLOW_CONTROL_NONE,
                   blueQ_serial_parity_bits parity_bits = BLUEQ_SERIAL_PARITY_BITS_NONE,
                   blueQ_serial_stop_bits stop_bits = BLUEQ_SERIAL_STOP_BITS_ONE) {
    return blueQ_connect_IUT_serial(port, rate, flow_control, parity_bits, stop_bits);
}

inline bluespy_error set_config(const char* IXIT_file, const char* ICS_file = nullptr,
                                const void* options = nullptr) {
    return blueQ_set_config(IXIT_file, ICS_file, options);
}
} // namespace blueQ

#endif

#endif

