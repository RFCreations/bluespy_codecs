#include "stdint.h"

/*------------------------------------------------------------------------------
 * AVDTP Media Codec Capability structures
 * Derived from Specifcation Documents as indicated
 *----------------------------------------------------------------------------*/

/**
 * @brief AVDTP Service Category identifiers (AVDTP Specification, Section 8.21, Service
 * Capabilities)
 */
typedef enum AVDTP_Service_Category_E : uint8_t {
    AVDTP_Service_Not_Applicable = 0,
    AVDTP_Service_Media_Transport = 1,
    AVDTP_Service_Reporting = 2,
    AVDTP_Service_Recovery = 3,
    AVDTP_Service_Content_Protection = 4,
    AVDTP_Service_Header_Compression = 5,
    AVDTP_Service_Multiplexing = 6,
    AVDTP_Service_Media_Codec = 7,
    AVDTP_Service_Delay_Reporting = 8,
} AVDTP_SERVICE_CATEGORY_E;

/**
 * @brief AVDTP Media Type Values (Assigned Numbers, Section 6.3.1, Media Type)
 */
typedef enum AVDTP_Media_Type_e : uint8_t {
    AVDTP_MediaType_Audio = 0,
    AVDTP_MediaType_Video = 1,
    AVDTP_MediaType_Multimedia = 2,
} AVDTP_MEDIA_TYPE_E;

/**
 * @brief AVDTP Media Codec Types (Assigned Numbers, Section 6.5.1, Audio Codec ID)
 */
typedef enum AVDTP_MEDIA_CODEC_TYPE_E : uint8_t {
    AVDTP_Codec_SBC = 0,
    AVDTP_Codec_MPEG_12_Audio = 1,
    AVDTP_Codec_MPEG_24_AAC = 2,
    AVDTP_Codec_MPEG_D_USAC = 3,
    AVDTP_Codec_ATRAC_Family = 4,
    AVDTP_Codec_Vendor_Specific = 0xFF
} AVDTP_MEDIA_CODEC_TYPE_E;

/**
 * @brief AVDTP Media Codec Service Capability structure (AVDTP Specification, Section 8.21.5, Media
 * Codec Capabilities)
 *
 * Represents the "Media Codec" capability element signalled in A2DP.
 *
 * If @ref Media_Codec_Type is @ref AVDTP_Codec_Vendor_Specific, then the
 * first four bytes of @ref Media_Codec_Specific_Information encode a 32-bit
 * Vendor ID (big-endian), and the following byte encodes a Vendor-specific Codec ID.
 *
 * @note This structure uses a flexible array member. The actual size depends on
 * the Length_Of_Service_Capabilities field.
 */
typedef struct AVDTP_Service_Capabilities_Media_Codec_t {
    AVDTP_SERVICE_CATEGORY_E Service_Category;
    uint8_t Length_Of_Service_Capabilities;
    uint8_t RFU : 4;
    AVDTP_MEDIA_TYPE_E Media_Type : 4;
    AVDTP_MEDIA_CODEC_TYPE_E Media_Codec_Type;
    uint8_t Media_Codec_Specific_Information[1];
} AVDTP_Service_Capabilities_Media_Codec_t;

/*------------------------------------------------------------------------------
 * LE Audio Codec Specific Configuration Containers
 * See Specifcation Documents as indicated
 *----------------------------------------------------------------------------*/

/**
 * @brief LE Audio Codec Specific Configuration container
 *
 * Encapsulates the LTV (Length-Type-Value) sequence carried in an ASE
 * Codec_Specific_Configuration. This mirrors the on-air LE Audio format.
 *
 * The flexible array member @ref Codec_Specific_Information holds one or more
 * concatenated LEA_LTV blocks:
 *
 *     +---------+------+------ ... ----+
 *     | Length  | Type | Value bytes…  |
 *     +---------+------+------ ... ----+
 */
typedef struct LEA_Codec_Specific_Config_t {
    uint8_t Codec_ID[5]; // <- Coding Format (1 byte), RFU-Company ID (2 bytes), RFU-Vendor specific
                         // codec ID (2 bytes). See Assigned Numbers Section 2.11.
    uint8_t Cap_Length;  // <- Length of following LTVs in bytes
    uint8_t Codec_Specific_Information[1]; // <- The LTVs
} LEA_Codec_Specific_Config_t;

/**
 * @brief LE Audio Broadcast (BIS) configuration container
 *
 * Matches the concatenated ACAD (Big Info) + AdvData (BASE) region obtained
 * from extended advertising PDUs for a broadcast isochronous stream.
 *
 * Within the BASE portion, the LC3 codec-specific configuration LTVs follow the
 * 2-byte 0x1851 service UUID. These TLVs have identical semantics to those in
 * @ref LEA_Codec_Specific_Config_t (types 1 - 5).
 */
typedef struct LEA_Broadcast_Codec_Config_t {
    uint8_t acad_len;    /*!< Length of ACAD block including type byte (filled by host) */
    uint8_t acad_type;   /*!< Should be 0x2C (BIG Info) */
    uint8_t big_info[1]; /*!< Variable-length BIG Info payload(s) followed by AdvData */
} LEA_Broadcast_Codec_Config_t;