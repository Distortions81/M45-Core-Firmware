# Third-Party Notices

Project-owned firmware and source code are licensed under the MIT License.
This project includes or links against third-party software when built with
native ESP-IDF. Keep these notices with source or binary redistributions.

The firmware management UI serves a `/licenses` page with this attribution
summary and third-party license references.

## ESP-IDF

- Source: https://github.com/espressif/esp-idf
- License: Apache License 2.0 for Espressif original code
- License documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/COPYRIGHT.html

ESP-IDF includes firmware components under additional third-party licenses.
Espressif's ESP-IDF license documentation states that source-code headers take
precedence over summary license lists. Examples of ESP-IDF firmware components
used directly or indirectly by this firmware include:

- FreeRTOS: MIT License
- lwIP: BSD License
- Newlib: BSD License
- Mbed TLS via ESP-IDF/WPA supplicant: Apache License 2.0 in this ESP-IDF
  build context

The Apache License 2.0 text used for ESP-IDF is included with this project at
`LICENSES/third-party/Apache-2.0.txt`. Review source headers and component
license files in the ESP-IDF checkout for authoritative component-level terms.

## QRCode

- Source: https://github.com/ricmoo/QRCode
- License: MIT License
- Copyright: Richard Moore
- Includes portions derived from Project Nayuki's QR Code generator library

The QRCode MIT License text is included with this project at
`LICENSES/third-party/QRCode.txt`.
