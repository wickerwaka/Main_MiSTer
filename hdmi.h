#if !defined(HDMI_H)
#define HDMI_H 1

struct vmode_custom_t;

void hdmi_config_init();

void hdmi_config_set_spd(bool val);
void hdmi_config_set_spare(int packet, bool enabled);
void hdmi_config_set_mode(vmode_custom_t *vm);
void hdmi_config_set_hdr();

const uint8_t *hdmi_get_active_edid();


/*
union EDIDDescriptor
{
    uint8_t data[18];
} __attribute__((packed));

struct EDID
{
    // 0-19
    uint8_t magic[8];
    
    uint8_t man_id[2];
    uint16_t man_product;
    uint32_t man_serial;
    uint8_t man_week;
    uint8_t man_year;

    uint8_t edid_version;
    uint8_t edid_revision;

    // 20-24
    uint8_t display_input;
    uint8_t display_horizontal;
    uint8_t display_vertical;
    uint8_t display_gamma;
    uint8_t display_features;

    // 25-34
    uint8_t chromacity[10];

    // 35-37
    uint8_t timing_bitmap[3];

    // 38-53
    uint16_t standard_timing[8];

    // 54-125
    EDIDDescriptor descriptor[4];

    // 126-127
    uint8_t extension_count;
    
    uint8_t checksum;

    uint8_t extension[128];
} __attribute__((packed));

static_assert(sizeof(EDID) == 256);
*/

#endif // HDMI_H