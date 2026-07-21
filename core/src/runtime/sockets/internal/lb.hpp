/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_LB_HPP_INCLUDED__
#define __ZLINK_LB_HPP_INCLUDED__

#include <map>
#include <vector>

#include "utils/array.hpp"

namespace zlink
{
class msg_t;
class pipe_t;

//  This class manages a set of outbound pipes. On send it load balances
//  messages fairly among the pipes.

class lb_t
{
  public:
    lb_t ();
    ~lb_t ();

    void attach (pipe_t *pipe_);
    void activated (pipe_t *pipe_);
    void pipe_terminated (pipe_t *pipe_);
    void set_weight (pipe_t *pipe_, uint32_t weight_);
    uint32_t weight (pipe_t *pipe_) const;
    bool has_positive_weight_pipe () const;
    bool contains (pipe_t *pipe_) const;

    int send (msg_t *msg_);

    //  Sends a message and stores the pipe that was used in pipe_.
    //  It is possible for this function to return success but keep pipe_
    //  unset if the rest of a multipart message to a terminated pipe is
    //  being dropped. For the first frame, this will never happen.
    int sendpipe (msg_t *msg_, pipe_t **pipe_);

    bool has_out ();

  private:
    //  List of outbound pipes.
    typedef array_t<pipe_t, 2> pipes_t;
    pipes_t _pipes;

    //  Number of active pipes. All the active pipes are located at the
    //  beginning of the pipes array.
    pipes_t::size_type _active;

    //  Points to the last pipe that the most recent message was sent to.
    pipes_t::size_type _current;

    //  True if last we are in the middle of a multipart message.
    bool _more;

    //  True if we are dropping current message.
    bool _dropping;
    std::map<pipe_t *, uint32_t> _weights;
    bool _weighted_dirty;
    bool _weighted_enabled;
    std::vector<pipe_t *> _weighted_schedule;
    size_t _weighted_current;
    pipe_t *_weighted_multipart_pipe;

    void deactivate (pipe_t *pipe_);
    void mark_weighted_dirty ();
    void rebuild_weighted_schedule ();

    ZLINK_NON_COPYABLE_NOR_MOVABLE (lb_t)
};
}

#endif
