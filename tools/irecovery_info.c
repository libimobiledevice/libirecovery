/*
 * irecovery.c
 * Software frontend for iBoot/iBSS communication with iOS devices
 *
 * Copyright (c) 2012-2020 Nikias Bassen <nikias@gmx.li>
 * Copyright (c) 2012-2015 Martin Szulecki <martin.szulecki@libimobiledevice.org>
 * Copyright (c) 2010-2011 Chronic-Dev Team
 * Copyright (c) 2010-2011 Joshua Hill
 * Copyright (c) 2008-2011 Nicolas Haunold
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define TOOL_NAME "irecovery_info"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <libirecovery.h>
#include <readline/readline.h>
#include <readline/history.h>



static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};

static int mod_table[] = {0, 2, 1};

static const uint8_t hextable[] = {
   ['0'] = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, // faster for most modern processors,
   ['A'] = 10, 11, 12, 13, 14, 15,       // for the space conscious, reduce to
   ['a'] = 10, 11, 12, 13, 14, 15        // signed char.
};

uint64_t hexDecode(const char* input) {
  uint64_t result = 0;
  while (*input && result >= 0) {
     result = (result << 4) | hextable[(uint8_t)*input++];
  }

  return result;
}

char *base64Encode(const unsigned char *data,
                    size_t input_length,
                    size_t *output_length) {

    *output_length = 4 * ((input_length + 2) / 3);

    char *encoded_data = malloc(*output_length);
    if (encoded_data == NULL) return NULL;

    for (size_t i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    return encoded_data;
}

void outputPlist(const irecv_device_info_t *info) {
  printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n");
  printf("<plist version=\"1.0\">\n<dict>\n");

  printf("\t<key>CPID</key>\n\t<integer>%d</integer>\n", info->cpid);
  printf("\t<key>CPRV</key>\n\t<integer>%d</integer>\n", info->cprv);
  printf("\t<key>CPFM</key>\n\t<integer>%d</integer>\n", info->cpfm);
  printf("\t<key>SCEP</key>\n\t<integer>%d</integer>\n", info->scep);
  printf("\t<key>BDID</key>\n\t<integer>%d</integer>\n", info->bdid);

  printf("\t<key>ECID</key>\n\t<string>%llX</string>\n", info->ecid);

  printf("\t<key>IBFL</key>\n\t<integer>%d</integer>\n", info->ibfl);
  printf("\t<key>SRTG</key>\n\t<string>%s</string>\n", info->srtg);

  size_t apNonceLength;
  char* apNonce = base64Encode(info->ap_nonce, info->ap_nonce_size, &apNonceLength);
  printf("\t<key>APNonce</key>\n\t<data>%s</data>\n", apNonce);

  size_t sepNonceLength;
  char* sepNonce = base64Encode(info->sep_nonce, info->sep_nonce_size, &sepNonceLength);
  printf("\t<key>SEPNonce</key>\n\t<data>%s</data>\n", sepNonce);

  printf("</dict>\n</plist>\n");
}

int main(int argc, char* argv[]) {
  if ((argc != 2) || strlen(argv[1]) != 16) {
    fprintf(stderr, "Usage: irecovery_info <ECID>\n");
    return -1;
  }

  irecv_client_t client;
  uint64_t ecid = hexDecode(argv[1]);

  irecv_error_t result = irecv_open_with_ecid_and_attempts(&client, ecid, 10);
  if (result != IRECV_E_SUCCESS) {
    fprintf(stderr, "Unable to open device with ECID %llX (error %d)\n", ecid, result);
  }

  const irecv_device_info_t* deviceInfo = irecv_get_device_info(client);

  outputPlist(deviceInfo);

  return 0;
}
