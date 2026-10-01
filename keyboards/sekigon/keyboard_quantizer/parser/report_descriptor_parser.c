// Copyright 2023 sekigon-gonnoc
// SPDX-License-Identifier: GPL-2.0-or-later

#include "report_descriptor_parser.h"
#include "report_descriptor_tags.h"

#include <stddef.h>
#include <string.h> // memset

#ifdef __AVR__
#    define NO_PRINT
#endif

#include "debug.h"
#include "print.h"

#define LEN(x) (sizeof(x) / sizeof(x[0]))

hid_device_t hid_device_collection[HID_DEVICE_COUNT];
hid_id_collection_t hid_id_collections[HID_ID_COLLECTION_COUNT];
hid_report_member_t hid_report_definitions[HID_REPORT_MEMBER_COUNT];

hid_device_t const *get_hid_device_def(uint8_t interface) {
  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    if (hid_device_collection[idx].id_collection != NULL &&
        hid_device_collection[idx].interface == interface) {
      return &hid_device_collection[idx];
    }
  }

  return NULL;
}

static bool hid_interface_has(uint8_t interface, uint16_t page_usage) {
  hid_device_t const *device = get_hid_device_def(interface);
  if (device == NULL) {
    return false;
  }
  hid_id_collection_t const *collection = device->id_collection;
  while (collection != NULL) {
    uint16_t usage_id = (uint16_t)((collection->usage_page << 8) | collection->usage);
    if (usage_id == page_usage) {
      return true;
    }
    collection = collection->next;
  }
  return false;
}

bool hid_interface_is_mouse(uint8_t interface) {
  return hid_interface_has(interface, 0x0102);
}

bool hid_interface_is_keyboard(uint8_t interface) {
  return hid_interface_has(interface, 0x0106);
}

bool hid_has_mouse(void) {
  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    if (hid_device_collection[idx].id_collection != NULL &&
        hid_interface_is_mouse(hid_device_collection[idx].interface)) {
      return true;
    }
  }
  return false;
}

static hid_device_t *find_empty_device(void) {
  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    if (hid_device_collection[idx].id_collection == NULL) {
      return &hid_device_collection[idx];
    }
  }

  // full
  return NULL;
}

static hid_id_collection_t *find_empty_id_idx_begin(void) {
  for (int idx = 0; idx < LEN(hid_id_collections); idx++) {
    if (hid_id_collections[idx].report_def_start == NULL) {
      return &hid_id_collections[idx];
    }
  }

  // full
  return NULL;
}

static hid_report_member_t *find_empty_report_def(void) {
  for (int idx = 0; idx < LEN(hid_report_definitions); idx++) {
    if (!hid_report_definitions[idx].in_use) {
      return &hid_report_definitions[idx];
    }
  }

  // full
  return NULL;
}

static void free_report_def(hid_report_member_t *member) {
  if (member != NULL) {
    memset(member, 0, sizeof(*member));
  }
}

static bool append_report_member(hid_id_collection_t *collection, hid_report_member_t **current_member,
                                 hid_report_member_t *new_member) {
  if (collection == NULL || new_member == NULL || current_member == NULL) {
    return false;
  }
  if (*current_member == new_member || collection->report_def_start == new_member) {
    /* A self-link would make parse_report()'s member walk loop forever. */
    return false;
  }
  if (collection->report_def_start == NULL) {
    collection->report_def_start = new_member;
  } else if (*current_member != NULL) {
    (*current_member)->next = new_member;
  } else {
    hid_report_member_t *tail = collection->report_def_start;
    while (tail->next != NULL) {
      tail = tail->next;
    }
    tail->next = new_member;
  }
  *current_member = new_member;
  return true;
}

/* A Report ID may come back later in the same Application collection
 * (1 -> 2 -> 1). Its fields belong to the list that ID already has, otherwise
 * parse_report() finds the first list and the later fields are unreachable. */
static hid_id_collection_t *find_linked_id_collection(hid_device_t const *hid_device, uint8_t id) {
  if (hid_device == NULL || id == 0) {
    return NULL;
  }
  hid_id_collection_t *collection = hid_device->id_collection;
  while (collection != NULL) {
    if (collection->id == id) {
      return collection;
    }
    collection = collection->next;
  }
  return NULL;
}

static void link_id_collection(hid_device_t *hid_device, hid_id_collection_t *collection) {
  if (hid_device == NULL || collection == NULL) {
    return;
  }
  if (hid_device->id_collection == NULL) {
    hid_device->id_collection = collection;
    return;
  }
  hid_id_collection_t *tail = hid_device->id_collection;
  while (tail->next != NULL) {
    if (tail == collection) {
      return;
    }
    tail = tail->next;
  }
  if (tail != collection) {
    tail->next = collection;
  }
}

/* Makes *collection the list that report ID `id` owns. One Application
 * collection often holds several Report IDs (buttons+xy vs wheel); each ID
 * keeps its own list so a short wheel packet is not parsed as "all buttons
 * released". A list with no fields yet just takes the ID. */
static void select_id_list(hid_device_t *hid_device, hid_id_collection_t **collection,
                           hid_report_member_t **current_member, uint8_t id) {
  hid_id_collection_t *cur = *collection;
  /* ID 0 means "no ID", which cannot follow IDs in one descriptor. */
  if (cur == NULL || cur->id == id || id == 0) {
    return;
  }
  if (id != 0 && cur->report_def_start != NULL && cur->id != 0) {
    link_id_collection(hid_device, cur);
    hid_id_collection_t *existing = find_linked_id_collection(hid_device, id);
    if (existing != NULL) {
      /* Seen before: keep appending to the list this ID already owns.
       * current_member = NULL makes append_report_member() walk to its tail
       * instead of splicing after the previous list. */
      *collection     = existing;
      *current_member = NULL;
      return;
    }
    hid_id_collection_t *next = find_empty_id_idx_begin();
    if (next == NULL || next == cur) {
      return; /* out of lists: keep filling the current one */
    }
    memset(next, 0, sizeof(*next));
    next->usage      = cur->usage;
    next->usage_page = cur->usage_page;
    next->id         = id;
    *collection      = next;
    *current_member  = NULL;
    return;
  }
  cur->id = id;
}

static void delete_hid_id_collection(hid_id_collection_t *collection) {
  if (collection == NULL) {
    return;
  }

  // delete report defs
  hid_report_member_t *next = collection->report_def_start;
  while (next != NULL) {
    hid_report_member_t *del = next;
    next = del->next;
    free_report_def(del);
  }

  memset(collection, 0, sizeof(*collection));
}

void delete_hid_device(uint8_t interface) {
  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    if (hid_device_collection[idx].id_collection != NULL &&
        hid_device_collection[idx].interface == interface) {
      hid_id_collection_t *next = hid_device_collection[idx].id_collection;
      while (next != NULL) {
        hid_id_collection_t *del = next;
        next = next->next;
        delete_hid_id_collection(del);
      }

      memset(&hid_device_collection[idx], 0,
             sizeof(hid_device_collection[idx]));
    }
  }
}

/* Local items last only until the next Main item (HID 1.11 6.2.2.8). Clearing
 * them after Input alone let a Usage survive into the next Output, Feature or
 * Collection. */
static void reset_local_state(hid_report_member_t *member, uint16_t *usage_table, size_t usage_table_len,
                              uint16_t *usage_table_idx) {
  hid_report_local_member_t empty = {0};
  member->local = empty;
  *usage_table_idx = 0;
  memset(usage_table, 0, usage_table_len);
}

static bool get_next_item(uint8_t const **buf, uint16_t *const len,
                          hid_report_item_t *const item) {
  if (*len == 0 || *buf == NULL) {
    return false;
  }

  uint8_t const *data = *buf;

  item->prefix = data[0];
  item->tag = (data[0] >> 4) & 0x0F;
  item->size = (data[0]) & 0x03;

  if (item->tag == 0x0F) {
    /* Long item: prefix, bDataSize, bLongItemTag, then bDataSize bytes
     * (HID 1.11 6.2.2.3), so the whole item is 3 + bDataSize long. */
    if (*len < 3 || *len < (uint16_t)(3 + data[1])) {
      *len = 0;
      return false;
    }
    item->size = data[1];
    item->data = 0;
    item->raw = 0;
    *buf += (uint16_t)(3 + item->size);
  } else {
    /* Short item size code 3 means four data bytes. */
    uint8_t nbytes = (item->size == 3) ? 4 : item->size;
    if (*len < (uint16_t)(1 + nbytes)) {
      *len = 0;
      return false;
    }

    (*buf)++;
    item->raw = 0;
    for (uint8_t i = 0; i < nbytes; i++) {
      item->raw |= ((uint32_t)data[1 + i]) << (8 * i);
    }
    item->size = nbytes; /* keep the real width, not the encoded size code */
    *buf += nbytes;
  }

  /* Sign extend from the encoded width. Callers that want the unsigned value
   * (Usage IDs, Report Size/Count) read item->raw instead. */
  item->data = (int32_t)item->raw;
  if (item->size > 0 && item->size < 4) {
    uint32_t sign_bit = (uint32_t)1 << (item->size * 8 - 1);
    if (item->raw & sign_bit) {
      item->data = (int32_t)(item->raw | ~((sign_bit << 1) - 1));
    }
  }

  if (*buf - data > *len) {
    *len = 0;
    return false;
  } else {
    *len = *len - (*buf - data);
    return true;
  }
}

bool parse_report_descriptor(uint8_t interface, uint8_t const *desc,
                             uint16_t len) {
  if (desc == NULL) {
    len = 0;
  }

  uint8_t const *buf = desc;
  uint16_t remain_len = len;

  hid_report_item_t item = {0};
  hid_report_member_t member = {0};

  delete_hid_device(interface);

  hid_device_t *hid_device = find_empty_device();

  if (hid_device == NULL) {
    dprintln("No empty hid device table");
    return false;
  }

  hid_device->interface = interface;
  hid_device->id_collection = NULL;

  // Initialize first collection
  hid_report_member_t *current_member = NULL;
  hid_id_collection_t *current_collection = NULL;
  int8_t level = 0;
  uint16_t usage_table[HID_USAGE_COUNT] = {0};
  uint16_t usage_table_idx = 0;
  uint16_t top_usage_page = 0;
  uint16_t top_usage = 0;
  hid_report_member_t global_stack[4] = {0};
  uint8_t global_sp = 0;

  while (get_next_item(&buf, &remain_len, &item)) {
    for (int idx = 0; idx < level; idx++) {
      dprintf("  ");
    }
    dprintf("0x%02X 0x%08lX\t\t\t", (unsigned)item.prefix, (unsigned long)item.raw);

    switch (item.prefix & (HID_RI_TYPE_MASK | HID_RI_TAG_MASK)) {
    case HID_RI_COLLECTION(0):
      dprintln("Start collection");

      if (level == 0) {
        current_collection = find_empty_id_idx_begin();
        if (current_collection == NULL) {
          dprintln("No empty collection");
          break;
        }
        memset(current_collection, 0, sizeof(*current_collection));
        dprintf("Register to collection %p\n", (void *)current_collection);

        current_collection->usage = top_usage;
        current_collection->usage_page = top_usage_page;
        /* A Report ID given before the Collection applies to it. */
        current_collection->id = member.global.report_id;
        current_member = NULL;
      }
      reset_local_state(&member, usage_table, sizeof(usage_table), &usage_table_idx);
      level++;

      break;

    case HID_RI_END_COLLECTION(0):
      dprintln("End collection");

      if (level == 1) {
        if (current_collection != NULL &&
            current_collection->report_def_start != NULL) {
          dprintf("New collection defined %p\n", (void *)current_collection);
          link_id_collection(hid_device, current_collection);
        }
        current_collection = NULL;
        current_member = NULL;
      }
      reset_local_state(&member, usage_table, sizeof(usage_table), &usage_table_idx);

      level--;
      if (level < 0) {
        level = 0;
      }
      break;

    case HID_RI_REPORT_ID(0):
      dprintf("Report id %u\n", (unsigned)item.raw);
      /* Only recorded: the list is picked when an Input uses it. A Report ID
       * that only Feature/Output items use (often between Push and Pop) must
       * not take over the list of the Input items around it. */
      member.global.report_id = (uint8_t)item.raw;
      break;

    case HID_RI_USAGE_PAGE(0):
      dprintf("Usage Page %u\n", (unsigned)item.raw);
      member.global.usage_page = (uint16_t)item.raw;
      if (level == 0) {
        /* Also the identity of the collection this page precedes. */
        top_usage_page = (uint16_t)item.raw;
      }
      break;

    case HID_RI_USAGE(0):
      dprintf("Usage %u\n", (unsigned)item.raw);

      if (usage_table_idx >= LEN(usage_table)) {
        dprintln("Error");
        break;
      }

      if (level == 0) {
        top_usage = (uint16_t)item.raw;
      } else {
        usage_table[usage_table_idx] = (uint16_t)item.raw;
        usage_table_idx++;
      }

      break;

    /* Usage IDs are unsigned (HID 1.11 6.2.2.8), so take raw rather than the
     * sign-extended data. A one-byte Usage Minimum of 0xE0 (the modifier
     * block) was being stored as -32; the keyboard parser only survived it
     * because it casts the sum back to uint8_t. Logical Minimum / Maximum
     * below are signed and keep using item.data. */
    case HID_RI_USAGE_MINIMUM(0):
      dprintf("Usage minimum %u\n", (unsigned)item.raw);

      member.local.usage_minimum = (int32_t)item.raw;
      break;

    case HID_RI_USAGE_MAXIMUM(0):
      dprintf("Usage maximum %u\n", (unsigned)item.raw);

      member.local.usage_maximum = (int32_t)item.raw;
      break;

    case HID_RI_LOGICAL_MINIMUM(0):
      dprintf("Logical minimum %ld\n", (long)item.data);

      member.global.logical_minimum = item.data;
      break;

    case HID_RI_LOGICAL_MAXIMUM(0):
      dprintf("Logical maximum %ld\n", (long)item.data);

      /* Signed only when the minimum is: 0..255 is commonly written as
       * 15 00 25 FF, where FF must not read as -1 (Linux decides the same). */
      member.global.logical_maximum = member.global.logical_minimum < 0 ? item.data : (int32_t)item.raw;
      break;

    case HID_RI_REPORT_SIZE(0):
      dprintf("Report size %u\n", (unsigned)item.raw);

      member.global.report_size = (uint8_t)item.raw;
      break;

    case HID_RI_REPORT_COUNT(0):
      dprintf("Report count %u\n", (unsigned)item.raw);

      member.global.report_count = (uint8_t)item.raw;
      break;

    case HID_RI_PUSH(0):
      dprintln("Push");
      if (global_sp < LEN(global_stack)) {
        global_stack[global_sp++] = member;
      }
      break;

    case HID_RI_POP(0):
      dprintln("Pop");
      if (global_sp > 0) {
        member.global = global_stack[--global_sp].global;
        if (level == 0) {
          /* Usage Page is part of the restored global table, and at level 0 it
           * is also what names the collection that follows. */
          top_usage_page = member.global.usage_page;
        }
      }
      break;

    case HID_RI_INPUT(0):
      dprintf("Input %u\n", (unsigned)item.raw);
      // apply member
      usage_table_idx = usage_table_idx > 0 ? usage_table_idx : 1;
      /* In an Array input (bit 1 clear) a report value picks one of the
       * usages. Usages listed one by one in a consecutive run are the same as
       * that range (09 E9 09 EA = Usage Minimum E9, Maximum EA), which is what
       * the report parser resolves array values against. */
      if ((item.raw & 0x02) == 0 && usage_table_idx > 1 &&
          member.local.usage_maximum <= member.local.usage_minimum) {
        bool run = true;
        for (uint16_t i = 1; i < usage_table_idx; i++) {
          if (usage_table[i] != (uint16_t)(usage_table[i - 1] + 1)) {
            run = false;
          }
        }
        if (run) {
          member.local.usage_minimum = usage_table[0];
          member.local.usage_maximum = usage_table[usage_table_idx - 1];
          usage_table[0]             = 0;
          usage_table_idx            = 1;
        }
      }
      if (current_collection != NULL) {
        select_id_list(hid_device, &current_collection, &current_member, member.global.report_id);
      }
      if (current_collection == NULL) {
        dprintln("Input with no collection");
      } else if (member.global.report_count == 0 || member.global.report_size == 0) {
        /* HID data items always have report_size and report_count >= 1. */
        dprintln("Input size/count 0");
      } else {
        uint8_t saved_count = member.global.report_count;
        uint8_t n_usages    = (uint8_t)usage_table_idx;
        if (n_usages > saved_count && saved_count > 0) {
          n_usages = saved_count;
        }
        uint8_t per = (n_usages > 0) ? (uint8_t)(saved_count / n_usages) : saved_count;
        if (per == 0) {
          per = 1;
        }
        member.global.report_count = per;

        for (int idx = 0; idx < n_usages; idx++) {
          hid_report_member_t *new_member = find_empty_report_def();

          if (new_member == NULL) {
            dprintln("No empty table");
            break;
          } else {
            dprintf("Register to report table %p\n", (void *)new_member);
          }

          member.local.usage = usage_table[idx];
          *new_member        = member;
          new_member->in_use = true;
          new_member->next   = NULL;

          dprintln("Apply new member");
          if (!append_report_member(current_collection, &current_member, new_member)) {
            free_report_def(new_member);
            break;
          }
        }

        uint8_t used     = (uint8_t)(per * n_usages);
        if (saved_count > used) {
          hid_report_member_t *pad = find_empty_report_def();
          if (pad != NULL) {
            *pad                     = member;
            pad->in_use              = true;
            pad->next                = NULL;
            pad->global.usage_page   = 0;
            pad->local.usage         = 0;
            pad->local.usage_minimum = 0;
            pad->local.usage_maximum = 0;
            pad->global.report_count = (uint8_t)(saved_count - used);
            if (!append_report_member(current_collection, &current_member, pad)) {
              free_report_def(pad);
            }
          }
        }

        member.global.report_count = saved_count;
      }

      reset_local_state(&member, usage_table, sizeof(usage_table), &usage_table_idx);

      break;

    case HID_RI_OUTPUT(0):
      dprintf("Output %u\n", (unsigned)item.raw);
      reset_local_state(&member, usage_table, sizeof(usage_table), &usage_table_idx);
      break;

    case HID_RI_FEATURE(0):
      dprintf("Feature %u\n", (unsigned)item.raw);
      reset_local_state(&member, usage_table, sizeof(usage_table), &usage_table_idx);
      break;

    default:
      dprintln("Not implemented");
      break;
    }
  }

  //
  // Compress collections
  //
  /* Unfinished top-level collection is never linked; free it on both success and failure. */
  if (current_collection != NULL) {
    delete_hid_id_collection(current_collection);
    current_collection = NULL;
  }

  if (hid_device->id_collection == NULL) {
    memset(hid_device, 0, sizeof(*hid_device));
    return false;
  }

  hid_id_collection_t *base = hid_device->id_collection;
  hid_id_collection_t *collection = hid_device->id_collection->next;

  while (collection != NULL) {
    if (collection->id == 0) {
      hid_report_member_t *tail = base->report_def_start;

      if (tail == NULL)
        break;

      while (tail->next != NULL) {
        tail = tail->next;
      }
      tail->next = collection->report_def_start;

      hid_id_collection_t *del = collection;
      collection = collection->next;
      base->next = collection;
      memset(del, 0, sizeof(*del));
    } else {
      base = collection;
      collection = base->next;
    }
  }

  dprintln("New HID is defined");

  return true;
}

void print_hid_report_member(hid_report_member_t const *member) {
  // clang-format off
  dprintf("{"
         "\tSize:%d Count:%d\n"
         "\tUsagePage:%d Usage:%d\n"
         "\tLogicalMin:%d Max:%d\n"
         "\tUsageMin:%ld Max:%ld\n"
         "}",
         (int)member->global.report_size, (int)member->global.report_count,
         (int)member->global.usage_page, (int)member->local.usage,
         (int)member->global.logical_minimum, (int)member->global.logical_maximum,
         (long)member->local.usage_minimum, (long)member->local.usage_maximum
        );
  // clang-format on
}

void print_collection(hid_id_collection_t const *collection) {
  dprintf("ID:%u, USAGE_PAGE:%u, USAGE:%u\n", (unsigned)collection->id, (unsigned)collection->usage_page,
          (unsigned)collection->usage);
  hid_report_member_t const *member = collection->report_def_start;

  while (member != NULL) {
    print_hid_report_member(member);
    member = member->next;
  }
  dprintf("\n");
}

void print_hid_device(uint8_t interface) {
  dprintf("Print interface:%d\n", (int)interface);

  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    hid_device_t const *device = &hid_device_collection[idx];
    if (device->id_collection != NULL && device->interface == interface) {
      hid_id_collection_t const *collection = device->id_collection;
      while (collection != NULL) {
        print_collection(collection);
        collection = collection->next;
      }
    }
  }

  dprintf("\n");
  dprintf("\n");
}

static const char *cli_hid_collection_kind(uint16_t usage_page, uint16_t usage) {
  uint16_t id = (uint16_t)((usage_page << 8) | usage);
  switch (id) {
    case 0x0106:
      return "keyboard";
    case 0x0102:
      return "mouse";
    case 0x0180:
      return "system";
    case 0x0C01:
      return "consumer";
    default:
      return "other";
  }
}

void print_hid_devices_cli(void) {
  uint8_t found = 0;
  for (int idx = 0; idx < LEN(hid_device_collection); idx++) {
    hid_device_t const *device = &hid_device_collection[idx];
    if (device->id_collection == NULL) {
      continue;
    }
    found++;
    printf("if:%u\n", (unsigned)device->interface);
    hid_id_collection_t const *collection = device->id_collection;
    while (collection != NULL) {
      printf("  id:%u page:0x%04X usage:0x%04X %s\n", (unsigned)collection->id, (unsigned)collection->usage_page,
             (unsigned)collection->usage, cli_hid_collection_kind(collection->usage_page, collection->usage));
      hid_report_member_t const *member = collection->report_def_start;
      while (member != NULL) {
        printf("    size:%u count:%u page:0x%04X usage:0x%04X min:%d max:%d umin:%ld umax:%ld\n",
               (unsigned)member->global.report_size, (unsigned)member->global.report_count,
               (unsigned)member->global.usage_page, (unsigned)member->local.usage, (int)member->global.logical_minimum,
               (int)member->global.logical_maximum, (long)member->local.usage_minimum,
               (long)member->local.usage_maximum);
        member = member->next;
      }
      collection = collection->next;
    }
  }
  if (found == 0) {
    printf("no HID descriptors\n");
  }
}
