##
## This file is part of the sigrok project.
##
## Copyright (C) 2011-2012 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
##

# Macronix MX25Lxx05D SPI (NOR) flash chip protocol decoder

# Note: Works for MX25L1605D/MX25L3205D/MX25L6405D.

import sigrokdecode as srd

# States
IDLE = -1

# Chip commands (also used as additional decoder states).
CMD_WREN      = 0x06
CMD_WRDI      = 0x04
CMD_RDID      = 0x9f
CMD_RDSR      = 0x05
CMD_WRSR      = 0x01
CMD_READ      = 0x03
CMD_FAST_READ = 0x0b
CMD_2READ     = 0xbb
CMD_SE        = 0x20
CMD_BE        = 0xd8
CMD_CE        = 0x60
CMD_CE2       = 0xc7
CMD_PP        = 0x02
CMD_CP        = 0xad
CMD_DP        = 0xb9
# CMD_RDP       = 0xab
# CMD_RES       = 0xab
CMD_RDP_RES   = 0xab # Note: RDP/RES have the same ID.
CMD_REMS      = 0x90
CMD_REMS2     = 0xef
CMD_ENSO      = 0xb1
CMD_EXSO      = 0xc1
CMD_RDSCUR    = 0x2b
CMD_WRSCUR    = 0x2f
CMD_ESRY      = 0x70
CMD_DSRY      = 0x80

# TODO: (Short) command names as strings in a dict, too?

# Dict which maps command IDs to their description.
cmds = {
    CMD_WREN: 'Write enable',
    CMD_WRDI: 'Write disable',
    CMD_RDID: 'Read identification',
    CMD_RDSR: 'Read status register',
    CMD_WRSR: 'Write status register',
    CMD_READ: 'Read data',
    CMD_FAST_READ: 'Fast read data',
    CMD_2READ: '2x I/O read',
    CMD_SE: 'Sector erase',
    CMD_BE: 'Block erase',
    CMD_CE: 'Chip erase',
    CMD_CE2: 'Chip erase', # Alternative command ID
    CMD_PP: 'Page program',
    CMD_CP: 'Continuously program mode',
    CMD_DP: 'Deep power down',
    # CMD_RDP: 'Release from deep powerdown',
    # CMD_RES: 'Read electronic ID',
    CMD_RDP_RES: 'Release from deep powerdown / Read electronic ID',
    CMD_REMS: 'Read electronic manufacturer & device ID',
    CMD_REMS2: 'Read ID for 2x I/O mode',
    CMD_ENSO: 'Enter secured OTP',
    CMD_EXSO: 'Exit secured OTP',
    CMD_RDSCUR: 'Read security register',
    CMD_WRSCUR: 'Write security register',
    CMD_ESRY: 'Enable SO to output RY/BY#',
    CMD_DSRY: 'Disable SO to output RY/BY#',
}

device_name = {
    0x14: 'MX25L1605D',
    0x15: 'MX25L3205D',
    0x16: 'MX25L6405D',
}

class Decoder(srd.Decoder):
    api_version = 1
    id = 'mx25lxx05d'
    name = 'MX25Lxx05D'
    longname = 'Macronix MX25Lxx05D'
    desc = 'Macronix MX25Lxx05D SPI flash chip decoder'
    license = 'gplv2+'
    inputs = ['spi', 'logic']
    outputs = ['mx25lxx05d']
    probes = []
    optional_probes = [
        {'id': 'hold', 'name': 'HOLD#', 'desc': 'TODO.'},
        {'id': 'wp_acc', 'name': 'WP#/ACC', 'desc': 'TODO.'},
    ]
    options = {} # TODO
    annotations = [
        ['TODO', 'TODO'],
    ]

    def __init__(self, **kwargs):
        self.state = IDLE
        self.cmdstate = 1 # TODO

    def start(self, metadata):
        # self.out_proto = self.add(srd.OUTPUT_PROTO, 'mx25lxx05d')
        self.out_ann = self.add(srd.OUTPUT_ANN, 'mx25lxx05d')

    def report(self):
        pass

    def putx(self, data):
        # Simplification, most annotations span extactly one SPI byte/packet.
        self.put(self.ss, self.es, self.out_ann, data)

    def handle_wren(self, mosi, miso):
        self.putx([0, ['Command: %s' % cmds[self.cmd]]])
        self.state = IDLE

    # TODO: Check/display device ID / name
    def handle_rdid(self, mosi, miso):
        if self.cmdstate == 1:
            # Byte 1: Master sends command ID.
            self.start_sample = self.ss
            self.putx([0, ['Command: %s' % cmds[self.cmd]]])
        elif self.cmdstate == 2:
            # Byte 2: Slave sends the JEDEC manufacturer ID.
            self.putx([0, ['Manufacturer ID: 0x%02x' % miso]])
        elif self.cmdstate == 3:
            # Byte 3: Slave sends the memory type (0x20 for this chip).
            self.putx([0, ['Memory type: 0x%02x' % miso]])
        elif self.cmdstate == 4:
            # Byte 4: Slave sends the device ID.
            self.device_id = miso
            self.putx([0, ['Device ID: 0x%02x' % miso]])

        if self.cmdstate == 4:
            # TODO: Check self.device_id is valid & exists in device_names.
            # TODO: Same device ID? Check!
            d = 'Device: Macronix %s' % device_name[self.device_id]
            self.put(self.start_sample, self.es, self.out_ann, [0, [d]])
            self.state = IDLE
        else:
            self.cmdstate += 1

    # TODO: Warn/abort if we don't see the necessary amount of bytes.
    # TODO: Warn if WREN was not seen before.
    def handle_se(self, mosi, miso):
        if self.cmdstate == 1:
            # Byte 1: Master sends command ID.
            self.addr = 0
            self.start_sample = self.ss
            self.putx([0, ['Command: %s' % cmds[self.cmd]]])
        elif self.cmdstate in (2, 3, 4):
            # Bytes 2/3/4: Master sends address of the sector to erase.
            # Note: Assumes SPI data is 8 bits wide (it is for MX25Lxx05D).
            # TODO: LSB-first of MSB-first?
            self.addr <<= 8
            self.addr |= mosi
            self.putx([0, ['Address byte %d: 0x%02x' % (self.cmdstate - 1,
                        miso)]]) # TODO: Count from 0 or 1?

        if self.cmdstate == 4:
            d = 'Erase sector %d' % self.addr
            self.put(self.start_sample, self.es, self.out_ann, [0, [d]])
            # TODO: Max. size depends on chip, check that too if possible.
            if self.addr % 4096 != 0:
                # Sector addresses must be 4K-aligned (same for all 3 chips).
                d = 'Warning: Invalid sector address!' # TODO: type == WARN?
                self.put(self.start_sample, self.es, self.out_ann, [0, [d]])
            self.state = IDLE
        else:
            self.cmdstate += 1

    def handle_rems(self, mosi, miso):
        if self.cmdstate == 1:
            # Byte 1: Master sends command ID.
            self.start_sample = self.ss
            self.putx([0, ['Command: %s' % cmds[self.cmd]]])
        elif self.cmdstate in (2, 3):
            # Bytes 2/3: Master sends two dummy bytes.
            # TODO: Check dummy bytes? Check reply from device?
            self.putx([0, ['Dummy byte: %s' % mosi]])
        elif self.cmdstate == 4:
            # Byte 4: Master sends 0x00 or 0x01.
            # 0x00: Master wants manufacturer ID as first reply byte.
            # 0x01: Master wants device ID as first reply byte.
            self.manufacturer_id_first = True if (mosi == 0x00) else False
            d = 'manufacturer' if (mosi == 0x00) else 'device'
            self.putx([0, ['Master wants %s ID first' % d]])
        elif self.cmdstate == 5:
            # Byte 5: Slave sends manufacturer ID (or device ID).
            self.ids = [miso]
            d = 'Manufacturer' if self.manufacturer_id_first else 'Device'
            self.putx([0, ['%s ID' % d]])
        elif self.cmdstate == 6:
            # Byte 6: Slave sends device ID (or manufacturer ID).
            self.ids.append(miso)
            d = 'Manufacturer' if self.manufacturer_id_first else 'Device'
            self.putx([0, ['%s ID' % d]])
        else:
            # TODO: Error?
            pass

        if self.cmdstate == 6:
            self.end_sample = self.es
            id = self.ids[1] if self.manufacturer_id_first else self.ids[0]
            self.putx([0, ['Device: Macronix %s' % device_name[id]]])
            self.state = IDLE
        else:
            self.cmdstate += 1

    def handle_rdsr(self, mosi, miso):
        self.putx([0, ['Command: %s (0x%02x)' % (cmds[self.cmd], miso)]])
        self.state = IDLE

    def decode(self, ss, es, data):

        ptype, mosi, miso = data

        if ptype != 'data':
            return

        cmd = mosi
        self.ss = ss
        self.es = es

        # If we encountered a known chip command, enter the resp. state.
        if self.state == IDLE:
            if cmd in cmds:
                self.state = cmd
                self.cmd = cmd # TODO: Eliminate?
                self.cmdstate = 1
            else:
                pass # TODO

        # Handle commands.
        # TODO: Use some generic way to invoke the resp. method.
        if self.state == CMD_WREN:
            self.handle_wren(mosi, miso)
        elif self.state == CMD_SE:
            self.handle_se(mosi, miso)
        elif self.state == CMD_RDID:
            self.handle_rdid(mosi, miso)
        if self.state == CMD_REMS:
            self.handle_rems(mosi, miso)
        if self.state == CMD_RDSR:
            self.handle_rdsr(mosi, miso)
        else:
            self.put(0, 0, self.out_ann, [0, ['Unknown command: 0x%02x' % cmd]])
            self.state = IDLE

