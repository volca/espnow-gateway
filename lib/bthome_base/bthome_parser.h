/*
 BTHome V1, V2 protocol packages parser

 Based on bthome_ble source parser by Ernst Klamer
 https://pypi.org/project/bthome-ble/

 Author: Attila Farago
 */

#pragma once

#include <functional>
#include "bthome_base_common.h"

namespace bthome_base
{

  using namespace std;

  typedef enum
  {
    BTProtoVersion_None = 0,
    BTProtoVersion_BTHomeV1 = 1,
    BTProtoVersion_BTHomeV2 = 2,
  } BTProtoVersion_e;

  using measurement_cb_fn_t = std::function<void(bthome_measurement_t measurement_type, float value)>;
  using log_cb_fn_t = std::function<void(const char *message)>;

  bool parse_payload_bthome(const uint8_t *payload_data, uint32_t payload_length, BTProtoVersion_e proto,
                            measurement_cb_fn_t measurement_cb, log_cb_fn_t log_cb);

}
