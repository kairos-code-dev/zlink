/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_MESSAGE_IO_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_MESSAGE_IO_INTERNAL_HPP_INCLUDED__

#include <zlink.h>

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class spot_node_t;
typedef std::deque<zlink_msg_t> spot_owned_msg_parts_t;

namespace spot_data_plane_message_io
{
int send_ascii_frame (socket_base_t *socket_,
                      const std::string &value_,
                      int flags_);
int send_control_snapshot (socket_base_t *socket_,
                           const char *topic_,
                           const std::string &target_endpoint_,
                           const std::string &source_node_id_,
                           const std::set<std::string> &filters_);
int send_snapshot_to_target (socket_base_t *socket_,
                             spot_node_t *node_,
                             const std::string &target_endpoint_);
int send_snapshot_to_peers (
  socket_base_t *socket_,
  spot_node_t *node_,
  const std::map<std::string, std::string> &peer_ctrl_endpoints_);
int send_ready_ack_snapshots_to_target (
  socket_base_t *socket_,
  const std::string &target_endpoint_,
  const std::map<std::string, std::map<std::string, std::set<std::string> > > &
    outbound_ready_filters_);
bool parse_ready_ack_arg (const std::string &arg_,
                          std::string *target_endpoint_out_,
                          std::string *raw_filter_out_,
                          std::string *ack_source_id_out_);
int recv_remaining_frame_strings (socket_base_t *socket_,
                                  std::vector<std::string> *out_);
int recv_remaining_frames_to_vector (socket_base_t *socket_,
                                     std::vector<zlink_msg_t> *out_,
                                     size_t *wire_bytes_out_);
int recv_remaining_frames_to_parts (socket_base_t *socket_,
                                    spot_owned_msg_parts_t *parts_out_,
                                    size_t *wire_bytes_out_);
}
}

#endif
