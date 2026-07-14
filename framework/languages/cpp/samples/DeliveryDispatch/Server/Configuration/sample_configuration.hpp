/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "sample_topology.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>
#include <string>

namespace zlink::samples::deliverydispatch
{

/* role별 값. courier actor node만 `nodeRid`를 채운다. */
struct sample_role_t
{
    std::string name;
    std::string log_dir;
    std::string node_rid;

    static sample_role_t bind (const zlink::framework::configuration_section_t &section)
    {
        sample_role_t role;
        role.name = section.require ("name");
        role.log_dir = section.require ("logDir");
        role.node_rid = section.get ("nodeRid").value_or ("");
        return role;
    }
};

struct sample_configuration_t
{
    sample_topology_t topology;
    sample_role_t role;

    std::string flow_log_path () const { return role.log_dir + "/flow-" + role.name + ".log"; }

    std::string evidence_path () const { return role.log_dir + "/evidence.log"; }
};

/* Framework host는 설정 파일 경로 하나만 받는다. 파일이 없거나 필수 값이 빠지면 runtime을
 * 시작하기 전에 실패한다(공통 정책 §2.1). */
inline sample_configuration_t
load_sample_configuration (zlink::framework::app_t &app, int argc, char **argv)
{
    app.config ().load_cli (argc, argv);
    const auto path = app.config ().model ().get ("config");
    if (!path) {
        throw std::runtime_error ("DeliveryDispatch role requires --config=<path>");
    }
    app.config ().load_json (*path);

    sample_configuration_t configuration;
    configuration.topology = app.config ().bind_required<sample_topology_t> ("sample.topology");
    configuration.role = app.config ().bind_required<sample_role_t> ("sample.role");
    return configuration;
}

} // namespace zlink::samples::deliverydispatch
