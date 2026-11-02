# License and third-party notices

ReflectorMic's original application code, web interface, build scripts, and documentation are licensed under the GNU General Public License, version 3 or (at your option) any later version. See [LICENSE](LICENSE). Existing file-level notices remain in effect.

The software is provided without warranty, as described in the license.

## Included components

- **MBELIB-neo R13:** the included D-Star codec subset is based on MBELIB-neo 2.6.2. Its source carries GPL-2.0-or-later notices, with additional file-specific notices where applicable. See [the codec license](lib/MbelibNeo/LICENSE), [license texts](lib/MbelibNeo/LICENSES), and [source provenance](lib/MbelibNeo/PROVENANCE.md). The GPL-2.0-or-later grant permits this combination under GPL-3.0-or-later.
- **PFFFT:** the bundled FFT implementation retains its BSD-style license and copyright notices. See [PFFFT's license](lib/MbelibNeo/pffft/LICENSE.txt).
- **ESP32-S3 codec adaptations:** files carrying GPL-2.0-or-later identifiers retain those terms and are combined into this GPL-3.0-or-later project.

The retained MBELIB-neo license collection includes an ETSI data notice referring to `src/tetra/tetra_acelp_codebooks.c`. That numeric codebook file is not part of this D-Star source distribution; its notice does not grant permission to add or redistribute those tables.

## External dependencies and media

M5Unified, M5GFX, Arduino-ESP32, and ESP-IDF are obtained separately through the project configuration and remain subject to their own licenses. Their license and copyright notices must accompany distributions where required.

The README links to a product photo hosted by M5Stack. That photo and M5Stack's trademarks remain the property of their respective rights holders and are not licensed under the ReflectorMic software license. No affiliation or endorsement is implied.
