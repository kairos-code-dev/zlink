#include <string.h>
#include <unistd.h>

#include <zlink.h>

#define CHECK(expr)                                                                                \
    do {                                                                                           \
        if (!(expr))                                                                               \
            return __LINE__;                                                                       \
    } while (0)

typedef struct join_completion_t
{
    int done;
    zlink_request_result_t result;
    int32_t join_result_code;
    zlink_actor_ref_t actor;
    zlink_routing_id_t joined_spot_rid;
    char reply[64];
} join_completion_t;

typedef struct entry_join_probe_t
{
    int accept;
    int handled;
    char request[64];
} entry_join_probe_t;

static int make_part (zlink_msg_t *msg, const char *text)
{
    const size_t len = strlen (text);
    CHECK (zlink_msg_init_size (msg, len) == ZLINK_CONFIG_OK);
    memcpy (zlink_msg_data (msg), text, len);
    return 0;
}

static void copy_part_text (zlink_msg_t *msg, char *out, size_t out_size)
{
    const size_t size = zlink_msg_size (msg);
    const size_t copy_size = size < out_size - 1 ? size : out_size - 1;
    memcpy (out, zlink_msg_data (msg), copy_size);
    out[copy_size] = '\0';
}

static zlink_routing_id_t make_routing_id (const char *text)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof rid);
    rid.size = (uint8_t) strlen (text);
    memcpy (rid.data, text, rid.size);
    return rid;
}

static int same_routing_id (const zlink_routing_id_t *lhs, const zlink_routing_id_t *rhs)
{
    return lhs->size == rhs->size && memcmp (lhs->data, rhs->data, lhs->size) == 0;
}

static int wait_for_completion (const join_completion_t *completion)
{
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (completion->done)
            return 1;
        usleep (10000);
    }
    return completion->done;
}

static void user_join_callback (const zlink_actor_join_result_t *result,
                                zlink_msg_t *parts,
                                size_t part_count,
                                void *userdata)
{
    join_completion_t *completion = (join_completion_t *) userdata;
    completion->done = 1;
    if (result != NULL) {
        completion->result = result->result;
        completion->join_result_code = result->join_result_code;
        completion->actor = result->actor;
        completion->joined_spot_rid = result->joined_spot_rid;
    }
    if (parts != NULL && part_count > 0)
        zlink_multipart_close (parts, part_count);
}

static void entry_join_callback (const zlink_actor_join_entry_spot_result_t *result,
                                 zlink_msg_t *parts,
                                 size_t part_count,
                                 void *userdata)
{
    join_completion_t *completion = (join_completion_t *) userdata;
    completion->done = 1;
    if (result != NULL) {
        completion->result = result->result;
        completion->join_result_code = result->join_result_code;
        completion->actor = result->actor;
        completion->joined_spot_rid = result->joined_spot_rid;
    }
    if (parts != NULL && part_count > 0) {
        copy_part_text (&parts[0], completion->reply, sizeof completion->reply);
        zlink_multipart_close (parts, part_count);
    }
}

static void reply_to_join (void *spot, int32_t join_result_code)
{
    zlink_actor_join_info_t info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_spot_actor_join_recv (spot, &info, &parts, &part_count, ZLINK_RECV_FLAGS_DONTWAIT)
        != ZLINK_RECV_OK)
        return;
    if (parts != NULL && part_count > 0)
        zlink_multipart_close (parts, part_count);

    zlink_msg_t reply;
    if (make_part (&reply, "user-join-reply") != 0)
        return;
    zlink_spot_actor_join_reply (spot, &info, join_result_code, &reply, 1);
}

static void user_dispatch (void *spot, const zlink_spot_dispatch_info_t *info, void *userdata)
{
    (void) info;
    (void) userdata;
    reply_to_join (spot, 0);
}

static void entry_dispatch (void *spot, const zlink_spot_dispatch_info_t *info, void *userdata)
{
    (void) info;
    entry_join_probe_t *probe = (entry_join_probe_t *) userdata;
    zlink_actor_join_info_t join_info;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    if (zlink_spot_actor_join_recv (spot, &join_info, &parts, &part_count, ZLINK_RECV_FLAGS_DONTWAIT)
        != ZLINK_RECV_OK)
        return;

    if (parts != NULL && part_count > 0)
        copy_part_text (&parts[0], probe->request, sizeof probe->request);
    if (parts != NULL && part_count > 0)
        zlink_multipart_close (parts, part_count);

    zlink_msg_t reply;
    if (make_part (&reply, probe->accept ? "entry-accepted" : "entry-rejected") != 0)
        return;
    zlink_spot_actor_join_reply (spot, &join_info, probe->accept ? 0 : 7, &reply, 1);
    probe->handled = 1;
}

int main (void)
{
    void *ctx = zlink_ctx_new ();
    CHECK (ctx != NULL);

    zlink_spot_node_options_t options;
    memset (&options, 0, sizeof options);
    options.mode = ZLINK_SPOT_NODE_MODE_ALL;
    void *node = zlink_spot_node_new (ctx, &options);
    CHECK (node != NULL);

    zlink_routing_id_t user_spot_rid = make_routing_id ("c-user-spot");
    void *user_spot = NULL;
    uint32_t created = 0;
    CHECK (zlink_spot_node_spot_get_or_new (node, &user_spot_rid, &user_spot, &created)
           == ZLINK_CONFIG_OK);
    CHECK (user_spot != NULL);
    CHECK (created == 1);
    void *entry_spot = NULL;
    CHECK (zlink_spot_node_entry_spot (node, &entry_spot) == ZLINK_CONFIG_OK);
    CHECK (entry_spot != NULL);
    zlink_routing_id_t entry_spot_rid;
    CHECK (zlink_get_routing_id (entry_spot, &entry_spot_rid) == ZLINK_CONFIG_OK);

    CHECK (zlink_spot_dispatch_event_handler (user_spot, user_dispatch, NULL) == ZLINK_HANDLER_OK);
    entry_join_probe_t entry_probe;
    memset (&entry_probe, 0, sizeof entry_probe);
    entry_probe.accept = 1;
    CHECK (zlink_spot_dispatch_event_handler (entry_spot, entry_dispatch, &entry_probe)
           == ZLINK_HANDLER_OK);

    zlink_msg_t create_request[2];
    CHECK (make_part (&create_request[0], "profile") == 0);
    CHECK (make_part (&create_request[1], "display-name") == 0);
    zlink_actor_ref_t created_with_request;
    CHECK (zlink_spot_node_actor_new_with_request (node, "c-create-payload-actor", create_request,
                                                   2, &created_with_request)
           == ZLINK_CONFIG_OK);

    zlink_spot_actor_lifecycle_event_t lifecycle_event;
    zlink_msg_t *lifecycle_parts = NULL;
    size_t lifecycle_part_count = 0;
    CHECK (zlink_spot_recv_actor_lifecycle_with_request (
             entry_spot, &lifecycle_event, &lifecycle_parts, &lifecycle_part_count,
             ZLINK_RECV_FLAGS_DONTWAIT)
           == ZLINK_RECV_OK);
    CHECK (lifecycle_event.kind == ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED);
    CHECK (strcmp (lifecycle_event.info.current_actor.actor_id,
                   created_with_request.actor_id)
           == 0);
    CHECK (lifecycle_part_count == 2);
    CHECK (zlink_msg_size (&lifecycle_parts[0]) == strlen ("profile"));
    CHECK (memcmp (zlink_msg_data (&lifecycle_parts[0]), "profile", strlen ("profile")) == 0);
    CHECK (zlink_msg_size (&lifecycle_parts[1]) == strlen ("display-name"));
    CHECK (memcmp (zlink_msg_data (&lifecycle_parts[1]), "display-name",
                   strlen ("display-name"))
           == 0);
    zlink_multipart_close (lifecycle_parts, lifecycle_part_count);

    zlink_actor_ref_t actor;
    CHECK (zlink_spot_node_actor_new (node, "c-entry-join-actor", &actor) == ZLINK_CONFIG_OK);

    zlink_msg_t user_request;
    CHECK (make_part (&user_request, "join-user") == 0);
    join_completion_t user_join;
    memset (&user_join, 0, sizeof user_join);
    CHECK (zlink_spot_node_actor_join_spot (node, &actor, &actor.node_rid, &user_spot_rid,
                                            &user_request, 1, user_join_callback, &user_join,
                                            ZLINK_DONTWAIT, 1000)
           == ZLINK_SUBMIT_OK);
    CHECK (wait_for_completion (&user_join) == 1);
    CHECK (user_join.result == ZLINK_REQUEST_OK);
    CHECK (user_join.join_result_code == 0);
    CHECK (same_routing_id (&user_join.joined_spot_rid, &user_spot_rid));

    zlink_msg_t entry_request;
    CHECK (make_part (&entry_request, "entry-accept-request") == 0);
    join_completion_t accepted;
    memset (&accepted, 0, sizeof accepted);
    CHECK (zlink_spot_node_actor_join_entry_spot (node, &actor, &actor.node_rid, &entry_request, 1,
                                                  entry_join_callback, &accepted, ZLINK_DONTWAIT,
                                                  1000)
           == ZLINK_SUBMIT_OK);
    CHECK (wait_for_completion (&accepted) == 1);
    CHECK (accepted.result == ZLINK_REQUEST_OK);
    CHECK (accepted.join_result_code == 0);
    CHECK (strcmp (accepted.reply, "entry-accepted") == 0);
    CHECK (strcmp (entry_probe.request, "entry-accept-request") == 0);
    CHECK (same_routing_id (&accepted.joined_spot_rid, &entry_spot_rid));

    memset (&user_join, 0, sizeof user_join);
    CHECK (zlink_spot_node_actor_join_spot (node, &actor, &actor.node_rid, &user_spot_rid,
                                            NULL, 0, user_join_callback, &user_join,
                                            ZLINK_DONTWAIT, 1000)
           == ZLINK_SUBMIT_OK);
    CHECK (wait_for_completion (&user_join) == 1);
    CHECK (user_join.result == ZLINK_REQUEST_OK);

    entry_probe.accept = 0;
    entry_probe.handled = 0;
    memset (entry_probe.request, 0, sizeof entry_probe.request);
    zlink_msg_t reject_request;
    CHECK (make_part (&reject_request, "entry-reject-request") == 0);
    join_completion_t rejected;
    memset (&rejected, 0, sizeof rejected);
    CHECK (zlink_spot_node_actor_join_entry_spot (node, &actor, &actor.node_rid, &reject_request, 1,
                                                  entry_join_callback, &rejected, ZLINK_DONTWAIT,
                                                  1000)
           == ZLINK_SUBMIT_OK);
    CHECK (wait_for_completion (&rejected) == 1);
    CHECK (rejected.result == ZLINK_REQUEST_OK);
    CHECK (rejected.join_result_code == 7);
    CHECK (strcmp (rejected.reply, "entry-rejected") == 0);
    CHECK (strcmp (entry_probe.request, "entry-reject-request") == 0);

    zlink_actor_ref_t rows[1];
    size_t row_count = 1;
    CHECK (zlink_spot_actors (user_spot, rows, &row_count) == ZLINK_CONFIG_OK);
    CHECK (row_count == 1);
    CHECK (strcmp (rows[0].actor_id, actor.actor_id) == 0);

    CHECK (zlink_spot_node_destroy (&node) == ZLINK_CLOSE_OK);
    CHECK (zlink_ctx_term (ctx) == ZLINK_CLOSE_OK);
    return 0;
}
