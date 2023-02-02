#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "cfg.h"
#include "hardware.h"
#include "mat4x4.h"
#include "profiling.h"
#include "smbus.h"
#include "user_io.h"
#include "video.h"



static void hdmi_config_set_csc()
{
	// default color conversion matrices
	// for the original hexadecimal versions please refer
	// to the ADV7513 programming guide section 4.3.7
	float ypbpr_coeffs[] = {
		0.42944335937f, 1.64038085938f, 1.93017578125f, 0.49389648437f,
		0.25683593750f, 0.50415039062f, 0.09790039062f, 0.06250f,
		1.85498046875f, 1.71557617188f, 0.42944335937f, 0.49389648437f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	// no transformation, so use identity matrix
	float hdmi_full_coeffs[] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	float hdmi_limited_1_coeffs[] = {
		0.8583984375f, 0.0f, 0.0f, 0.06250f,
		0.0f, 0.8583984375f, 0.0f, 0.06250f,
		0.0f, 0.0f, 0.8583984375f, 0.06250f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	float hdmi_limited_2_coeffs[] = {
		0.93701171875f, 0.0f, 0.0f, 0.06250f,
		0.0f, 0.93701171875f, 0.0f, 0.06250f,
		0.0f, 0.0f, 0.93701171875f, 0.06250f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	float hdr_bt2020_coeffs[] = {
		0.6274f, 0.3293f, 0.0433f, 0.0f,
		0.0691f, 0.9195f, 0.0114f, 0.0f,
		0.0164f, 0.0880f, 0.8956f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	float hdr_dcip3_coeffs[] = {
		0.8225f, 0.1774f, 0.0000f, 0.0f,
		0.0332f, 0.9669f, 0.0000f, 0.0f,
		0.0171f, 0.0724f, 0.9108f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};

	const float pi = float(M_PI);

	// select the base CSC
	int ypbpr = cfg.ypbpr && cfg.direct_video;
	int hdmi_limited_1 = cfg.hdmi_limited & 1;
	int hdmi_limited_2 = cfg.hdmi_limited & 2;
	int hdr = cfg.hdr;

	mat4x4 coeffs = hdmi_full_coeffs;

	if (hdr == 1)
		coeffs = hdr_bt2020_coeffs;
	else if (hdr == 2)
		coeffs = hdr_dcip3_coeffs;
	else if (ypbpr)
		coeffs = ypbpr_coeffs;
	else if (hdmi_limited_1)
		coeffs = hdmi_limited_1_coeffs;
	else if (hdmi_limited_2)
		coeffs = hdmi_limited_2_coeffs;
	else
		coeffs = hdmi_full_coeffs;

	mat4x4 csc(coeffs);

	// apply color controls
	float brightness = (((cfg.video_brightness/100.0f) - 0.5f)); // [-0.5 .. 0.5]
	float contrast = ((cfg.video_contrast/100.0f) - 0.5f) * 2.0f + 1.0f; // [0 .. 2]
	float saturation = ((cfg.video_saturation/100.0f)); // [0 .. 1]
	float hue = (cfg.video_hue * pi / 180.0f);

	char* gain_offset = cfg.video_gain_offset;

	// we have to parse these
	float gain_red = 1;
	float gain_green = 1;
	float gain_blue = 1;
	float off_red = 0;
	float off_green = 0;
	float off_blue = 0;

	size_t target = 0;
	float* targets[6] = { &gain_red, &off_red, &gain_green, &off_green, &gain_blue, &off_blue };

	for (size_t i = 0; i < strlen(gain_offset) && target < 6; i++)
	{
		// skip whitespace
		if (gain_offset[i] == ' ' || gain_offset[i] == ',')
			continue;

		int numRead = 0;
		int match = sscanf(gain_offset + i, "%f%n", targets[target], &numRead);

		i += numRead > 0 ? numRead - 1 : 0;

		if (match == 1)
			target++;
	}

	// first apply hue matrix, because it does not touch luminance
	float cos_hue = cos(hue);
	float sin_hue = sin(hue);
	float lr = 0.213f;
	float lg = 0.715f;
	float lb = 0.072f;
	float ca = 0.143f;
	float cb = 0.140f;
	float cc = 0.283f;

	mat4x4 mat_hue;
	mat_hue.setIdentity();

	mat_hue.m11 = lr+cos_hue*(1-lr)+sin_hue*(-lr);
	mat_hue.m12 = lg+cos_hue*(-lg) +sin_hue*(-lg);
	mat_hue.m13 = lb+cos_hue*(-lb) +sin_hue*(1-lb);

	mat_hue.m21 = lr+cos_hue*(-lr) +sin_hue*(ca);
	mat_hue.m22 = lg+cos_hue*(1-lg)+sin_hue*(cb);
	mat_hue.m23 = lb+cos_hue*(-lb) +sin_hue*(cc);

	mat_hue.m31 = lr+cos_hue*(-lr) +sin_hue*(-(1-lr));
	mat_hue.m32 = lg+cos_hue*(-lg) +sin_hue*(lg);
	mat_hue.m33 = lb+cos_hue*(1-lb)+sin_hue*(lb);

	csc = csc * mat_hue;

	// now saturation
	float s = saturation;
	float sr = ( 1.0f - s ) * .3086f;
	float sg = ( 1.0f - s ) * .6094f;
	float sb = ( 1.0f - s ) * .0920f;

	float mat_saturation[] = {
		sr + s, sg, sb, 0,
		sr, sg + s, sb, 0,
		sr, sg, sb + s, 0,
		0, 0, 0, 1.0f
	};

	csc = csc * mat4x4(mat_saturation);

	// now brightness and contrast
	float b = brightness;
	float c = contrast;
	float t = (1.0f - c) / 2.0f;

	float mat_brightness_contrast[] = {
		c, 0, 0, (t+b),
		0, c, 0, (t+b),
		0, 0, c, (t+b),
		0, 0, 0, 1.0f
	};

	csc = csc * mat4x4(mat_brightness_contrast);

	// gain and offset
	float rg = gain_red;
	float ro = off_red;
	float gg = gain_green;
	float go = off_green;
	float bg = gain_blue;
	float bo = off_blue;

	float mat_gain_off[] = {
		rg, 0, 0, ro,
		0, gg, 0, go,
		0, 0, bg, bo,
		0, 0, 0, 1.0f
	};

	csc = csc * mat4x4(mat_gain_off);

	// final compression
	csc.compress(2.0f);

	// finally, apply a fixed multiplier to get it in
	// correct range for ADV7513 chip
	const int16_t csc_int16[12] = {
		int16_t(csc.comp[0] * 2048.0f),
		int16_t(csc.comp[1] * 2048.0f),
		int16_t(csc.comp[2] * 2048.0f),
		int16_t(csc.comp[3] * 2048.0f),
		int16_t(csc.comp[4] * 2048.0f),
		int16_t(csc.comp[5] * 2048.0f),
		int16_t(csc.comp[6] * 2048.0f),
		int16_t(csc.comp[7] * 2048.0f),
		int16_t(csc.comp[8] * 2048.0f),
		int16_t(csc.comp[9] * 2048.0f),
		int16_t(csc.comp[10] * 2048.0f),
		int16_t(csc.comp[11] * 2048.0f),
	};

	// Clamps to reinforce limited if necessary
	// 0x100 = 16/256 * 4096 (12-bit mul)
	// 0xEB0 = 235/256 * 4096
	// 0xFFF = 4095 (12-bit max)
	uint16_t clipMin = (hdmi_limited_1 || hdmi_limited_2) ? 0x100 : 0x000;
	uint16_t clipMax = hdmi_limited_1 ? 0xEB0 : 0xFFF;

	// pass to HDMI, use 0xA0 to set a mode of [-2 .. 2] per ADV7513 programming guide
	uint8_t csc_data[] = {
		0x18, (uint8_t)(0b10100000 | (( csc_int16[0] >> 8) & 0b00011111)),  // csc Coefficients, Channel A
		0x19, (uint8_t)(csc_int16[0] & 0xff),
		0x1A, (uint8_t)(csc_int16[1] >> 8),
		0x1B, (uint8_t)(csc_int16[1] & 0xff),
		0x1C, (uint8_t)(csc_int16[2] >> 8),
		0x1D, (uint8_t)(csc_int16[2] & 0xff),
		0x1E, (uint8_t)(csc_int16[3] >> 8),
		0x1F, (uint8_t)(csc_int16[3] & 0xff),

		0x20, (uint8_t)(csc_int16[4] >> 8),  // csc Coefficients, Channel B
		0x21, (uint8_t)(csc_int16[4] & 0xff),
		0x22, (uint8_t)(csc_int16[5] >> 8),
		0x23, (uint8_t)(csc_int16[5] & 0xff),
		0x24, (uint8_t)(csc_int16[6] >> 8),
		0x25, (uint8_t)(csc_int16[6] & 0xff),
		0x26, (uint8_t)(csc_int16[7] >> 8),
		0x27, (uint8_t)(csc_int16[7] & 0xff),

		0x28, (uint8_t)(csc_int16[8] >> 8),  // csc Coefficients, Channel C
		0x29, (uint8_t)(csc_int16[8] & 0xff),
		0x2A, (uint8_t)(csc_int16[9] >> 8),
		0x2B, (uint8_t)(csc_int16[9] & 0xff),
		0x2C, (uint8_t)(csc_int16[10] >> 8),
		0x2D, (uint8_t)(csc_int16[10] & 0xff),
		0x2E, (uint8_t)(csc_int16[11] >> 8),
		0x2F, (uint8_t)(csc_int16[11] & 0xff),

		0xC0, (uint8_t)(clipMin >> 8), // HDMI limited clamps
		0xC1, (uint8_t)(clipMin & 0xff),
		0xC2, (uint8_t)(clipMax >> 8),
		0xC3, (uint8_t)(clipMax & 0xff)
	};

	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		for (uint i = 0; i < sizeof(csc_data); i += 2)
		{
			int res = i2c_smbus_write_byte_data(fd, csc_data[i], csc_data[i + 1]);
			if (res < 0) printf("i2c: write error (%02X %02X): %d\n", csc_data[i], csc_data[i + 1], res);
		}

		i2c_close(fd);
	}
	else
	{
		printf("*** ADV7513 not found on i2c bus! HDMI won't be available!\n");
	}
}



void hdmi_config_init()
{
	int ypbpr = cfg.ypbpr && cfg.direct_video;

	// address, value
	uint8_t init_data[] = {
		0x98, 03,				// ADI required Write.

		0xD6, 0b11000000,		// [7:6] HPD Control...
								// 00 = HPD is from both HPD pin or CDC HPD
								// 01 = HPD is from CDC HPD
								// 10 = HPD is from HPD pin
								// 11 = HPD is always high

		0x41, 0x10,				// Power Down control
		0x9A, 0x70,				// ADI required Write.
		0x9C, 0x30,				// ADI required Write.
		0x9D, 0b01100001,		// [7:4] must be b0110!.
								// [3:2] b00 = Input clock not divided. b01 = Clk divided by 2. b10 = Clk divided by 4. b11 = invalid!
								// [1:0] must be b01!
		0xA2, 0xA4,				// ADI required Write.
		0xA3, 0xA4,				// ADI required Write.
		0xE0, 0xD0,				// ADI required Write.


		0x35, 0x40,
		0x36, 0xD9,
		0x37, 0x0A,
		0x38, 0x00,
		0x39, 0x2D,
		0x3A, 0x00,

		0x16, 0b00111000,		// Output Format 444 [7]=0.
								// [6] must be 0!
								// Colour Depth for Input Video data [5:4] b11 = 8-bit.
								// Input Style [3:2] b10 = Style 1 (ignored when using 444 input).
								// DDR Input Edge falling [1]=0 (not using DDR atm).
								// Output Colour Space RGB [0]=0.

		0x17, 0b01100010,		// Aspect ratio 16:9 [1]=1, 4:3 [1]=0, invert sync polarity

		0x3B, 0x0,              // Automatic pixel repetition and VIC detection


		0x48, 0b00001000,       // [6]=0 Normal bus order!
								// [5] DDR Alignment.
								// [4:3] b01 Data right justified (for YCbCr 422 input modes).

		0x49, 0xA8,				// ADI required Write.
		0x4A, 0b10000000, //Auto-Calculate SPD checksum
		0x4C, 0x00,				// ADI required Write.

		0x55, (uint8_t)(cfg.hdmi_game_mode ? 0b00010010 : 0b00010000),
								// [7] must be 0!. Set RGB444 in AVinfo Frame [6:5], Set active format [4].
								// AVI InfoFrame Valid [4].
								// Bar Info [3:2] b00 Bars invalid. b01 Bars vertical. b10 Bars horizontal. b11 Bars both.
								// Scan Info [1:0] b00 (No data). b01 TV. b10 PC. b11 None.

		0x56, (uint8_t)( 0b00001000 | (cfg.hdr ? 0xb11000000 : 0)),		// [5:4] Picture Aspect Ratio
								// [3:0] Active Portion Aspect Ratio b1000 = Same as Picture Aspect Ratio

		0x57, (uint8_t)((cfg.hdmi_game_mode ? 0x80 : 0x00)		// [7] IT Content. 0 - No. 1 - Yes (type set in register 0x59).
																// [6:4] Color space (ignored for RGB)
			| ((ypbpr || cfg.hdmi_limited) ? 0b0100 : cfg.hdr ? 0b1101000 : 0b0001000)),	// [3:2] RGB Quantization range
																// [1:0] Non-Uniform Scaled: 00 - None. 01 - Horiz. 10 - Vert. 11 - Both.

		0x59, (uint8_t)(cfg.hdmi_game_mode ? 0x30 : 0x00),		// [7:6] [YQ1 YQ0] YCC Quantization Range: b00 = Limited Range, b01 = Full Range
																// [5:4] IT Content Type b11 = Game, b00 = Graphics/None
																// [3:0] Pixel Repetition Fields b0000 = No Repetition

		0x73, 0x01,

		0x94, 0b10000000,       // [7]=1 HPD Interrupt ENabled.

		0x99, 0x02,				// ADI required Write.
		0x9B, 0x18,				// ADI required Write.

		0x9F, 0x00,				// ADI required Write.

		0xA1, 0b00000000,	    // [6]=1 Monitor Sense Power Down DISabled.

		0xA4, 0x08,				// ADI required Write.
		0xA5, 0x04,				// ADI required Write.
		0xA6, 0x00,				// ADI required Write.
		0xA7, 0x00,				// ADI required Write.
		0xA8, 0x00,				// ADI required Write.
		0xA9, 0x00,				// ADI required Write.
		0xAA, 0x00,				// ADI required Write.
		0xAB, 0x40,				// ADI required Write.

		0xAF, (uint8_t)(0b00000100	// [7]=0 HDCP Disabled.
								// [6:5] must be b00!
								// [4]=0 Current frame is unencrypted
								// [3:2] must be b01!
			| ((cfg.dvi_mode == 1) ? 0b00 : 0b10)),	 //	[1]=1 HDMI Mode.
								// [0] must be b0!

		0xB9, 0x00,				// ADI required Write.

		0xBA, 0b01100000,		// [7:5] Input Clock delay...
								// b000 = -1.2ns.
								// b001 = -0.8ns.
								// b010 = -0.4ns.
								// b011 = No delay.
								// b100 = 0.4ns.
								// b101 = 0.8ns.
								// b110 = 1.2ns.
								// b111 = 1.6ns.

		0xBB, 0x00,				// ADI required Write.
		0xDE, 0x9C,				// ADI required Write.
		0xE4, 0x60,				// ADI required Write.
		0xFA, 0x7D,				// Nbr of times to search for good phase

		// (Audio stuff on Programming Guide, Page 66)...
		0x0A, 0b00000000,		// [6:4] Audio Select. b000 = I2S.
								// [3:2] Audio Mode. (HBR stuff, leave at 00!).

		0x0B, 0b00001110,		//

		0x0C, 0b00000100,		// [7] 0 = Use sampling rate from I2S stream.   1 = Use samp rate from I2C Register.
								// [6] 0 = Use Channel Status bits from stream. 1 = Use Channel Status bits from I2C register.
								// [2] 1 = I2S0 Enable.
								// [1:0] I2S Format: 00 = Standard. 01 = Right Justified. 10 = Left Justified. 11 = AES.

		0x0D, 0b00010000,		// [4:0] I2S Bit (Word) Width for Right-Justified.
		0x14, 0b00000010,		// [3:0] Audio Word Length. b0010 = 16 bits.
		0x15, (uint8_t)((cfg.hdmi_audio_96k ? 0x80 : 0x00) | 0b0100000),	// I2S Sampling Rate [7:4]. b0000 = (44.1KHz). b0010 = 48KHz.
								// Input ID [3:1] b000 (0) = 24-bit RGB 444 or YCrCb 444 with Separate Syncs.

		// Audio Clock Config
		0x01, 0x00,				//
		0x02, (uint8_t)(cfg.hdmi_audio_96k ? 0x30 : 0x18),	// Set N Value 12288/6144
		0x03, 0x00,				//

		0x07, 0x01,				//
		0x08, 0x22,				// Set CTS Value 74250
		0x09, 0x0A,				//
	};

	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		for (uint i = 0; i < sizeof(init_data); i += 2)
		{
			int res = i2c_smbus_write_byte_data(fd, init_data[i], init_data[i + 1]);
			if (res < 0) printf("i2c: write error (%02X %02X): %d\n", init_data[i], init_data[i + 1], res);
		}

		i2c_close(fd);
	}
	else
	{
		printf("*** ADV7513 not found on i2c bus! HDMI won't be available!\n");
	}

	hdmi_config_set_csc();
}



void hdmi_config_set_spd(bool val)
{
	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		uint8_t packet_val = i2c_smbus_read_byte_data(fd, 0x40);
		if (val)
			packet_val |= 0x40;
		else
			packet_val &= ~0x40;
		int res = i2c_smbus_write_byte_data(fd, 0x40, packet_val);
		if (res < 0) printf("i2c: write error (%02X %02X): %d\n", 0x40, packet_val, res);
		i2c_close(fd);
	}
}



void hdmi_config_set_spare(int packet, bool enabled)
{
	int fd = i2c_open(0x39, 0);
	uint8_t mask = packet == 0 ? 0x01 : 0x02;
	if (fd >= 0)
	{
		uint8_t packet_val = i2c_smbus_read_byte_data(fd, 0x40);
		if (enabled)
			packet_val |= mask;
		else
			packet_val &= ~mask;
		int res = i2c_smbus_write_byte_data(fd, 0x40, packet_val);
		if (res < 0) printf("i2c: write error (%02X %02X): %d\n", 0x40, packet_val, res);
		i2c_close(fd);
	}
}



static uint8_t last_sync_invert = 0xff;
static uint8_t last_pr_flags = 0xff;
static uint8_t last_vic_mode = 0xff;

void hdmi_config_set_mode(vmode_custom_t *vm)
{
	PROFILE_FUNCTION();

	const uint8_t vic_mode = (uint8_t)vm->param.vic;
	uint8_t pr_flags;

	if (cfg.direct_video && is_menu()) pr_flags = 0; // automatic pixel repetition
	else if (vm->param.pr != 0) pr_flags = 0b01001000; // manual pixel repetition with 2x clock
	else pr_flags = 0b01000000; // manual pixel repetition

	uint8_t sync_invert = 0;
	if (vm->param.hpol == 0) sync_invert |= 1 << 5;
	if (vm->param.vpol == 0) sync_invert |= 1 << 6;

	if (last_sync_invert == sync_invert && last_pr_flags == pr_flags && last_vic_mode == vic_mode) return;

	// address, value
	uint8_t init_data[] = {
		0x17, (uint8_t)(0b00000010 | sync_invert),		// Aspect ratio 16:9 [1]=1, 4:3 [1]=0
		0x3B, pr_flags,
		0x3C, vic_mode,			// VIC
	};

	int fd = i2c_open(0x39, 0);
	if (fd >= 0)
	{
		for (uint i = 0; i < sizeof(init_data); i += 2)
		{
			int res = i2c_smbus_write_byte_data(fd, init_data[i], init_data[i + 1]);
			if (res < 0) printf("i2c: write error (%02X %02X): %d\n", init_data[i], init_data[i + 1], res);
		}

		i2c_close(fd);
	}
	else
	{
		printf("*** ADV7513 not found on i2c bus! HDMI won't be available!\n");
	}

	last_pr_flags = pr_flags;
	last_sync_invert = sync_invert;
	last_vic_mode = vic_mode;
}



void hdmi_config_set_hdr()
{
	// 87:01:1a:74:02:00:c2:33:c4:86:4c:1d:b8:0b:d0:84:80 :3e:13:3d:42:40:e8:03:32:00:e8:03:90:01
	uint8_t hdr_data[] = {
		0x87,
		0x01,
		0x1a,
		0x74,
		0x02,
		0x00,
		0xc2,
		0x33,
		0xc4,
		0x86,
		0x4c,
		0x1d,
		0xb8,
		0x0b,
		0xd0,
		0x84,
		0x80,
		0x3e,
		0x13,
		0x3d,
		0x42,
		0x40,
		0xe8,
		0x03,
		0x32,
		0x00,
		0xe8,
		0x03,
		0x90,
		0x01
	};

	if (cfg.hdr == 0)
	{
		hdmi_config_set_spare(1, false);
	}
	else
	{
		hdmi_config_set_spare(1, true);
		int fd = i2c_open(0x38, 0);
		int res = i2c_smbus_write_byte_data(fd, 0xFF, 0b10000000);
		if (res < 0)
		{
			printf("i2c: hdr: Couldn't update Spare Packet change register (0xDF, 0x80) %d\n", res);
		}

		uint8_t addr = 0xe0;
		for (uint i = 0; i < sizeof(hdr_data); i++)
		{
			res = i2c_smbus_write_byte_data(fd, addr, hdr_data[i]);
			if (res < 0) printf("i2c: hdr register write error (%02X %02x): %d\n", addr, hdr_data[i], res);
			addr += 1;
		}
		res = i2c_smbus_write_byte_data(fd, 0xfF, 0x00);
		if (res < 0) printf("i2c: hdr: Couldn't update Spare Packet change register (0xDF, 0x00), %d\n", res);
	}
}



static bool is_edid_valid(const uint8_t *edid)
{
	static const uint8_t magic[] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
	return !memcmp(edid, magic, sizeof(magic));
}



static uint8_t active_edid[256];
const uint8_t *hdmi_get_active_edid()
{
	if (is_edid_valid(active_edid)) return active_edid;

	int fd = i2c_open(0x39, 0);
	if (fd < 0)
	{
		printf("EDID: cannot find main i2c device\n");
		return 0;
	}

	//Test if adv7513 senses hdmi clock. If not, don't bother with the edid query
	int hpd_state = i2c_smbus_read_byte_data(fd, 0x42);
	if (hpd_state < 0 || !(hpd_state & 0x20))
	{
		i2c_close(fd);
		return 0;
	}


	for (int i = 0; i < 10; i++)
	{
		i2c_smbus_write_byte_data(fd, 0xC9, 0x03);
		i2c_smbus_write_byte_data(fd, 0xC9, 0x13);
	}
	i2c_close(fd);
	fd = i2c_open(0x3f, 0);
	if (fd < 0)
	{
		printf("EDID: cannot find i2c device.\n");
		return 0;
	}

	// waiting for valid EDID
	for (int k = 0; k < 20; k++)
	{
		for (uint i = 0; i < sizeof(active_edid); i++) active_edid[i] = (uint8_t)i2c_smbus_read_byte_data(fd, i);
		if (is_edid_valid(active_edid)) break;
		usleep(100000);
	}

	i2c_close(fd);
	printf("EDID:\n"); hexdump(active_edid, sizeof(active_edid), 0);

	if (!is_edid_valid(active_edid))
	{
		printf("Invalid EDID: incorrect header.\n");
		bzero(active_edid, sizeof(active_edid));
		return nullptr;
	}
	return active_edid;
}
