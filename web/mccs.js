// MCCS 2.2a VCP code definitions — generated from SPEC/MCCS 2.2a.pdf
window.MCCS = {
  groups: [
    { id: 'preset',   title: 'Preset Operations' },
    { id: 'image',    title: 'Image Adjustment' },
    { id: 'color',    title: 'Color Adjustment' },
    { id: 'display',  title: 'Display Control' },
    { id: 'geometry', title: 'Geometry' },
    { id: 'misc',     title: 'Miscellaneous' },
    { id: 'audio',    title: 'Audio' },
    { id: 'dpvl',     title: 'DPVL Support' }
  ],
  vcp: {}
};
(function (V) {
  function d(code, name, group, access, type, values, action) {
    V[code] = { name: name, group: group, access: access, type: type };
    if (values) V[code].values = values;
    if (action) V[code].action = true;
  }

  // ---- Preset Operations (Table 8-2) ----
  d(0x00, 'VCP Code Page', 'preset', 'rw', 'T');
  d(0x01, 'Degauss', 'misc', 'wo', 'NC', null, true);
  d(0x04, 'Restore Factory Defaults', 'preset', 'wo', 'NC', null, true);
  d(0x05, 'Restore Factory Luminance / Contrast Defaults', 'preset', 'wo', 'NC', null, true);
  d(0x06, 'Restore Factory Geometry Defaults', 'preset', 'wo', 'NC', null, true);
  d(0x08, 'Restore Factory Color Defaults', 'preset', 'wo', 'NC', null, true);
  d(0x0A, 'Restore Factory TV Defaults', 'preset', 'wo', 'NC', null, true);

  // ---- Image / Color Adjustment (Table 8-4) ----
  d(0x0B, 'User Color Temperature Increment', 'color', 'ro', 'NC');
  d(0x0C, 'User Color Temperature', 'color', 'rw', 'C');
  d(0x0E, 'Clock', 'image', 'rw', 'C');
  d(0x10, 'Luminance (Brightness)', 'image', 'rw', 'C');
  d(0x11, 'Flesh Tone Enhancement', 'color', 'rw', 'NC');
  d(0x12, 'Contrast', 'image', 'rw', 'C');
  d(0x13, 'Backlight Control (Legacy)', 'image', 'rw', 'C');
  d(0x14, 'Select Color Preset', 'color', 'rw', 'NC', {
    0x01: 'sRGB',
    0x02: 'Display Native',
    0x03: '4000 K',
    0x04: '5000 K',
    0x05: '6500 K',
    0x06: '7500 K',
    0x07: '8200 K',
    0x08: '9300 K',
    0x09: '10000 K',
    0x0A: '11500 K',
    0x0B: 'User 1',
    0x0C: 'User 2',
    0x0D: 'User 3'
  });
  d(0x16, 'Video Gain (Drive): Red', 'color', 'rw', 'C');
  d(0x17, 'User Color Vision Compensation', 'color', 'rw', 'C');
  d(0x18, 'Video Gain (Drive): Green', 'color', 'rw', 'C');
  d(0x1A, 'Video Gain (Drive): Blue', 'color', 'rw', 'C');
  d(0x1C, 'Focus', 'image', 'rw', 'C');
  d(0x1E, 'Auto Setup', 'image', 'rw', 'NC', {
    0x00: 'Auto setup not active',
    0x01: 'Perform auto setup',
    0x02: 'Enable continuous / periodic auto setup'
  });
  d(0x1F, 'Auto Color Setup', 'color', 'rw', 'NC', {
    0x00: 'Auto color setup not active',
    0x01: 'Perform auto color setup',
    0x02: 'Enable continuous / periodic auto color setup'
  });
  d(0x20, 'Horizontal Position (Phase)', 'geometry', 'rw', 'C');
  d(0x22, 'Horizontal Size', 'geometry', 'rw', 'C');
  d(0x24, 'Horizontal Pincushion', 'geometry', 'rw', 'C');
  d(0x26, 'Horizontal Pincushion Balance', 'geometry', 'rw', 'C');
  d(0x28, 'Horizontal Convergence R/B', 'geometry', 'rw', 'C');
  d(0x29, 'Horizontal Convergence M/G', 'geometry', 'rw', 'C');
  d(0x2A, 'Horizontal Linearity', 'geometry', 'rw', 'C');
  d(0x2C, 'Horizontal Linearity Balance', 'geometry', 'rw', 'C');
  d(0x2E, 'Gray Scale Expansion', 'color', 'rw', 'NC');
  d(0x30, 'Vertical Position (Phase)', 'geometry', 'rw', 'C');
  d(0x32, 'Vertical Size', 'geometry', 'rw', 'C');
  d(0x34, 'Vertical Pincushion', 'geometry', 'rw', 'C');
  d(0x36, 'Vertical Pincushion Balance', 'geometry', 'rw', 'C');
  d(0x38, 'Vertical Convergence R/B', 'geometry', 'rw', 'C');
  d(0x39, 'Vertical Convergence M/G', 'geometry', 'rw', 'C');
  d(0x3A, 'Vertical Linearity', 'geometry', 'rw', 'C');
  d(0x3C, 'Vertical Linearity Balance', 'geometry', 'rw', 'C');
  d(0x3E, 'Clock Phase', 'image', 'rw', 'C');
  d(0x40, 'Horizontal Parallelogram', 'geometry', 'rw', 'C');
  d(0x41, 'Vertical Parallelogram', 'geometry', 'rw', 'C');
  d(0x42, 'Horizontal Keystone', 'geometry', 'rw', 'C');
  d(0x43, 'Vertical Keystone', 'geometry', 'rw', 'C');
  d(0x44, 'Rotation', 'geometry', 'rw', 'C');
  d(0x46, 'Top Corner Flare', 'geometry', 'rw', 'C');
  d(0x48, 'Top Corner Hook', 'geometry', 'rw', 'C');
  d(0x4A, 'Bottom Corner Flare', 'geometry', 'rw', 'C');
  d(0x4C, 'Bottom Corner Hook', 'geometry', 'rw', 'C');
  d(0x52, 'Active Control', 'misc', 'ro', 'NC');
  d(0x54, 'Performance Preservation', 'misc', 'rw', 'NC');
  d(0x56, 'Horizontal Moiré', 'image', 'rw', 'C');
  d(0x58, 'Vertical Moiré', 'image', 'rw', 'C');
  d(0x59, '6 Axis Saturation Control: Red', 'color', 'rw', 'C');
  d(0x5A, '6 Axis Saturation Control: Yellow', 'color', 'rw', 'C');
  d(0x5B, '6 Axis Saturation Control: Green', 'color', 'rw', 'C');
  d(0x5C, '6 Axis Saturation Control: Cyan', 'color', 'rw', 'C');
  d(0x5D, '6 Axis Saturation Control: Blue', 'color', 'rw', 'C');
  d(0x5E, '6 Axis Saturation Control: Magenta', 'color', 'rw', 'C');

  // ---- Input / Audio (Table 8-13 / 8-15) ----
  d(0x60, 'Input Source', 'misc', 'rw', 'NC', {
    0x01: 'Analog video (RGB) 1',
    0x02: 'Analog video (RGB) 2',
    0x03: 'DVI 1',
    0x04: 'DVI 2',
    0x05: 'Composite video 1',
    0x06: 'Composite video 2',
    0x07: 'S-video 1',
    0x08: 'S-video 2',
    0x09: 'Tuner 1',
    0x0A: 'Tuner 2',
    0x0B: 'Tuner 3',
    0x0C: 'Component video (YPbPr / YCbCr) 1',
    0x0D: 'Component video (YPbPr / YCbCr) 2',
    0x0E: 'Component video (YPbPr / YCbCr) 3',
    0x0F: 'DisplayPort 1',
    0x10: 'DisplayPort 2',
    0x11: 'HDMI 1',
    0x12: 'HDMI 2'
  });
  d(0x62, 'Audio: Speaker Volume', 'audio', 'rw', 'NC');
  d(0x63, 'Audio: Speaker Select', 'audio', 'rw', 'NC', {
    0x00: 'FL/FR',
    0x01: 'SL/SR',
    0x02: 'RL/RR',
    0x03: 'FC/LFE',
    0x04: 'RC',
    0x05: 'FLC/FRC',
    0x06: 'RLC/RRC',
    0x07: 'FLW/FRW',
    0x08: 'FLH/FRH',
    0x09: 'TC',
    0x0A: 'FCH'
  });
  d(0x64, 'Audio: Microphone Volume', 'audio', 'rw', 'C');
  d(0x65, 'Audio: Jack Connection Status', 'audio', 'ro', 'NC');
  d(0x66, 'Ambient Light Sensor', 'misc', 'rw', 'NC', {
    0x01: 'Ambient light sensor disabled',
    0x02: 'Ambient light sensor enabled'
  });

  // ---- Backlight / Black Level / Gamma (Table 8-4) ----
  d(0x6B, 'Backlight Level: White', 'color', 'rw', 'C');
  d(0x6C, 'Video Black Level: Red', 'color', 'rw', 'C');
  d(0x6D, 'Backlight Level: Red', 'color', 'rw', 'C');
  d(0x6E, 'Video Black Level: Green', 'color', 'rw', 'C');
  d(0x6F, 'Backlight Level: Green', 'color', 'rw', 'C');
  d(0x70, 'Video Black Level: Blue', 'color', 'rw', 'C');
  d(0x71, 'Backlight Level: Blue', 'color', 'rw', 'C');
  d(0x72, 'Gamma', 'color', 'rw', 'NC');
  d(0x73, 'LUT Size', 'image', 'ro', 'T');
  d(0x74, 'Single Point LUT Operation', 'image', 'rw', 'T');
  d(0x75, 'Block LUT Operation', 'image', 'rw', 'T');
  d(0x76, 'Remote Procedure Call', 'misc', 'wo', 'T');
  d(0x78, 'Display Identification Data Operation', 'misc', 'ro', 'T');
  d(0x7C, 'Adjust Zoom', 'image', 'rw', 'C');

  // ---- Geometry / Image flips / scaling ----
  d(0x82, 'Horizontal Mirror (Flip)', 'geometry', 'rw', 'NC', {
    0x00: 'Normal mode',
    0x01: 'Mirrored horizontally'
  });
  d(0x84, 'Vertical Mirror (Flip)', 'geometry', 'rw', 'NC', {
    0x00: 'Normal mode',
    0x01: 'Mirrored vertically'
  });
  d(0x86, 'Display Scaling', 'geometry', 'rw', 'NC', {
    0x01: 'No scaling',
    0x02: 'Max Image (no AR distortion)',
    0x03: 'Max Vertical 1 (no AR distortion)',
    0x04: 'Max Horizontal 1 (no AR distortion)',
    0x05: 'Max Vertical 2 (AR distortion)',
    0x06: 'Max Horizontal 2 (AR distortion)',
    0x07: 'Full mode',
    0x08: 'Zoom mode',
    0x09: 'Squeeze mode',
    0x0A: 'Variable'
  });
  d(0x87, 'Sharpness', 'image', 'rw', 'C');
  d(0x88, 'Velocity Scan Modulation', 'image', 'rw', 'C');
  d(0x8A, 'Color Saturation', 'color', 'rw', 'C');
  d(0x8B, 'TV-Channel Up / Down', 'misc', 'wo', 'NC', {
    0x01: 'Increment channel',
    0x02: 'Decrement channel'
  });
  d(0x8C, 'TV-Sharpness', 'image', 'rw', 'C');
  d(0x8D, 'Audio: Mute / Screen Blank', 'audio', 'rw', 'NC', {
    0x01: 'Mute the audio',
    0x02: 'Un-mute the audio'
  });
  d(0x8E, 'TV-Contrast', 'image', 'rw', 'C');
  d(0x8F, 'Audio: Treble', 'audio', 'rw', 'NC');
  d(0x90, 'Hue', 'color', 'rw', 'C');
  d(0x91, 'Audio: Bass', 'audio', 'rw', 'NC');
  d(0x92, 'TV-Black Level / Luminance', 'color', 'rw', 'C');
  d(0x93, 'Audio: Balance L / R', 'audio', 'rw', 'NC');
  d(0x94, 'Audio: Processor Mode', 'audio', 'rw', 'NC', {
    0x01: 'Mono',
    0x02: 'Stereo',
    0x03: 'Stereo expanded',
    0x11: 'SRS 2.0',
    0x12: 'SRS 2.1',
    0x13: 'SRS 3.1',
    0x14: 'SRS 4.1',
    0x15: 'SRS 5.1',
    0x16: 'SRS 6.1',
    0x17: 'SRS 7.1',
    0x21: 'Dolby 2.0',
    0x22: 'Dolby 2.1',
    0x23: 'Dolby 3.1',
    0x24: 'Dolby 4.1',
    0x25: 'Dolby 5.1',
    0x26: 'Dolby 6.1',
    0x27: 'Dolby 7.1',
    0x31: 'THX 2.0',
    0x32: 'THX 2.1',
    0x33: 'THX 3.1',
    0x34: 'THX 4.1',
    0x35: 'THX 5.1',
    0x36: 'THX 6.1',
    0x37: 'THX 7.1'
  });
  d(0x95, 'Window Position (TL_X)', 'geometry', 'rw', 'C');
  d(0x96, 'Window Position (TL_Y)', 'geometry', 'rw', 'C');
  d(0x97, 'Window Position (BR_X)', 'geometry', 'rw', 'C');
  d(0x98, 'Window Position (BR_Y)', 'geometry', 'rw', 'C');
  d(0x9A, 'Window Background', 'image', 'rw', 'C');
  d(0x9B, '6 Axis Hue Control: Red', 'color', 'rw', 'C');
  d(0x9C, '6 Axis Hue Control: Yellow', 'color', 'rw', 'C');
  d(0x9D, '6 Axis Hue Control: Green', 'color', 'rw', 'C');
  d(0x9E, '6 Axis Hue Control: Cyan', 'color', 'rw', 'C');
  d(0x9F, '6 Axis Hue Control: Blue', 'color', 'rw', 'C');
  d(0xA0, '6 Axis Hue Control: Magenta', 'color', 'rw', 'C');
  d(0xA2, 'Auto Setup On / Off', 'image', 'wo', 'NC', {
    0x01: 'Turn auto setup off',
    0x02: 'Turn auto setup on'
  });
  d(0xA4, 'Window Mask Control', 'image', 'rw', 'T');
  d(0xA5, 'Window Select', 'image', 'rw', 'C', {
    0x00: 'Full display image area',
    0x01: 'Window 1',
    0x02: 'Window 2',
    0x03: 'Window 3',
    0x04: 'Window 4',
    0x05: 'Window 5',
    0x06: 'Window 6',
    0x07: 'Window 7'
  });
  d(0xA6, 'Window Size', 'image', 'rw', 'C');
  d(0xA7, 'Window Transparency', 'image', 'rw', 'C');
  d(0xAA, 'Screen Orientation', 'image', 'ro', 'NC', {
    0x01: '0 degrees (landscape)',
    0x02: '90 degrees (portrait)',
    0x03: '180 degrees (landscape)',
    0x04: '270 degrees (portrait)',
    0xFF: 'Not applicable'
  });

  // ---- Display Control (Table 8-9) ----
  d(0xAC, 'Horizontal Frequency', 'display', 'ro', 'C');
  d(0xAE, 'Vertical Frequency', 'display', 'ro', 'C');
  d(0xB0, 'Save / Restore Settings', 'preset', 'wo', 'NC', {
    0x01: 'Store Current Settings',
    0x02: 'Restore Defaults'
  }, true);
  d(0xB2, 'Flat Panel Sub-Pixel Layout', 'misc', 'ro', 'NC', {
    0x00: 'Sub-pixel layout not defined',
    0x01: 'RGB vertical stripe',
    0x02: 'RGB horizontal stripe',
    0x03: 'BGR vertical stripe',
    0x04: 'BGR horizontal stripe',
    0x05: 'Quad-pixel (red top left)',
    0x06: 'Quad-pixel (red bottom left)',
    0x07: 'Delta (triad)',
    0x08: 'Mosaic'
  });
  d(0xB4, 'Source Timing Mode', 'display', 'rw', 'T');
  d(0xB5, 'Source Color Coding', 'display', 'wo', 'NC', {
    0x00: 'RGB 4:4:4',
    0x01: 'YCbCr / YPbPr 4:4:4',
    0x02: 'YCbCr / YPbPr 4:2:2'
  });
  d(0xB6, 'Display Technology Type', 'misc', 'ro', 'NC', {
    0x01: 'CRT (shadow mask)',
    0x02: 'CRT (aperture grill)',
    0x03: 'LCD (active matrix)',
    0x04: 'LCoS',
    0x05: 'Plasma',
    0x06: 'OLED',
    0x07: 'EL',
    0x08: 'Dynamic MEM (e.g. DLP)',
    0x09: 'Static MEM (e.g. iMOD)'
  });

  // ---- DPVL Support (Table 8-17) ----
  d(0xB7, 'Monitor Status', 'dpvl', 'ro', 'NC');
  d(0xB8, 'Packet Count', 'dpvl', 'rw', 'C');
  d(0xB9, 'Monitor X Origin', 'dpvl', 'rw', 'C');
  d(0xBA, 'Monitor Y Origin', 'dpvl', 'rw', 'C');
  d(0xBB, 'Header Error Count', 'dpvl', 'rw', 'C');
  d(0xBC, 'Body CRC Error Count', 'dpvl', 'rw', 'C');
  d(0xBD, 'Client ID', 'dpvl', 'rw', 'C');
  d(0xBE, 'Link Control', 'dpvl', 'rw', 'NC', {
    0x00: 'Link shutdown disabled',
    0x01: 'Link shutdown enabled'
  });

  // ---- Display Control / Misc (Table 8-9 / 8-13) ----
  d(0xC0, 'Display Usage Time', 'display', 'ro', 'C');
  d(0xC2, 'Display Descriptor Length', 'misc', 'ro', 'C');
  d(0xC3, 'Transmit Display Descriptor', 'misc', 'rw', 'T');
  d(0xC4, "Enable Display of 'Display Descriptor'", 'misc', 'rw', 'NC', {
    0x01: 'Display enabled',
    0x02: 'Display disabled'
  });
  d(0xC6, 'Application Enable Key', 'misc', 'ro', 'NC');
  d(0xC7, 'Display Enable Key (Deprecated)', 'misc', 'wo', 'NC');
  d(0xC8, 'Display Controller ID', 'display', 'ro', 'NC');
  d(0xC9, 'Display Firmware Level', 'display', 'ro', 'C');
  d(0xCA, 'OSD / Button Event Control', 'display', 'rw', 'NC');
  d(0xCC, 'OSD Language', 'display', 'rw', 'NC', {
    0x01: 'Chinese (traditional / Hantai)',
    0x02: 'English',
    0x03: 'French',
    0x04: 'German',
    0x05: 'Italian',
    0x06: 'Japanese',
    0x07: 'Korean',
    0x08: 'Portuguese (Portugal)',
    0x09: 'Russian',
    0x0A: 'Spanish',
    0x0B: 'Swedish',
    0x0C: 'Turkish',
    0x0D: 'Chinese (simplified / Kantai)',
    0x0E: 'Portuguese (Brazil)',
    0x0F: 'Arabic',
    0x10: 'Bulgarian',
    0x11: 'Croatian',
    0x12: 'Czech',
    0x13: 'Danish',
    0x14: 'Dutch',
    0x15: 'Estonian',
    0x16: 'Finnish',
    0x17: 'Greek',
    0x18: 'Hebrew',
    0x19: 'Hindi',
    0x1A: 'Hungarian',
    0x1B: 'Latvian',
    0x1C: 'Lithuanian',
    0x1D: 'Norwegian',
    0x1E: 'Polish',
    0x1F: 'Romanian',
    0x20: 'Serbian',
    0x21: 'Slovak',
    0x22: 'Slovenian',
    0x23: 'Thai',
    0x24: 'Ukrainian',
    0x25: 'Vietnamese'
  });
  d(0xCD, 'Status Indicators (Host)', 'misc', 'rw', 'NC');
  d(0xCE, 'Auxiliary Display Size', 'misc', 'ro', 'NC');
  d(0xCF, 'Auxiliary Display Data', 'misc', 'wo', 'T');
  d(0xD0, 'Output Select', 'misc', 'rw', 'NC', {
    0x01: 'Analog video (RGB) 1',
    0x02: 'Analog video (RGB) 2',
    0x03: 'DVI 1',
    0x04: 'DVI 2',
    0x05: 'Composite video 1',
    0x06: 'Composite video 2',
    0x07: 'S-video 1',
    0x08: 'S-video 2',
    0x09: 'Tuner 1',
    0x0A: 'Tuner 2',
    0x0B: 'Tuner 3',
    0x0C: 'Component video (YPbPr / YCbCr) 1',
    0x0D: 'Component video (YPbPr / YCbCr) 2',
    0x0E: 'Component video (YPbPr / YCbCr) 3',
    0x0F: 'DisplayPort 1',
    0x10: 'DisplayPort 2',
    0x11: 'HDMI 1',
    0x12: 'HDMI 2'
  });
  d(0xD2, 'Asset Tag', 'misc', 'rw', 'T');
  d(0xD4, 'Stereo Video Mode', 'image', 'rw', 'NC');
  d(0xD6, 'Power Mode', 'display', 'rw', 'NC', {
    0x01: 'On',
    0x02: 'Standby',
    0x03: 'Suspend',
    0x04: 'Off (soft / DPMS off)',
    0x05: 'Off (hard / power off)'
  });
  d(0xD7, 'Auxiliary Power Output', 'misc', 'rw', 'NC', {
    0x01: 'Disable auxiliary output power',
    0x02: 'Enable auxiliary output power'
  });
  d(0xDA, 'Scan Mode', 'geometry', 'rw', 'NC', {
    0x00: 'Normal operation',
    0x01: 'Underscan',
    0x02: 'Overscan'
  });
  d(0xDB, 'Image Mode', 'display', 'rw', 'NC', {
    0x01: 'Full mode',
    0x02: 'Zoom mode',
    0x03: 'Squeeze mode',
    0x04: 'Variable'
  });
  d(0xDC, 'Display Application', 'image', 'rw', 'NC', {
    0x00: 'Standard / default mode',
    0x01: 'Productivity',
    0x02: 'Mixed',
    0x03: 'Movie',
    0x04: 'User defined',
    0x05: 'Games',
    0x06: 'Sports',
    0x07: 'Professional',
    0x08: 'Standard / default (intermediate power)',
    0x09: 'Standard / default (low power)',
    0x0A: 'Demonstration',
    0xF0: 'Dynamic contrast'
  });
  d(0xDE, 'Scratch Pad', 'misc', 'rw', 'NC');
  d(0xDF, 'VCP Version', 'display', 'ro', 'NC');

  // ---- Misc (Table 8-13) read/feedback ----
  d(0x02, 'New Control Value', 'misc', 'rw', 'NC', {
    0x01: 'No new control value(s)',
    0x02: 'One or more new control value(s) changed',
    0xFF: 'No user controls present'
  });
  d(0x03, 'Soft Controls', 'misc', 'ro', 'NC');
})(window.MCCS.vcp);
