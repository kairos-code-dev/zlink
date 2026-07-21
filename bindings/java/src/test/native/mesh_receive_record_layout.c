#include <stddef.h>

#include "zlink/service/dispatch.h"

_Static_assert(sizeof(zlink_mesh_receive_record_t) == 1200, "receive record size");
_Static_assert(offsetof(zlink_mesh_receive_record_t, source_binding_generation) == 528,
               "source binding generation offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, source_actor) == 536,
               "source actor offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, operation_id) == 1056,
               "operation id offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, kind_data) == 1160,
               "kind data offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, part_offset) == 1176,
               "part offset offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, part_count) == 1184,
               "part count offset");
_Static_assert(offsetof(zlink_mesh_receive_record_t, terminal_result) == 1192,
               "terminal result offset");

_Static_assert(sizeof(zlink_mesh_send_ready_data_t) == 1064,
               "send ready size");
_Static_assert(offsetof(zlink_mesh_send_ready_data_t, destination_kind) == 8,
               "send ready destination kind offset");
_Static_assert(offsetof(zlink_mesh_send_ready_data_t, target_actor) == 528,
               "send ready target actor offset");
_Static_assert(offsetof(zlink_mesh_send_ready_data_t, channel_name) == 1048,
               "send ready channel offset");

int main(void) {
  return 0;
}
