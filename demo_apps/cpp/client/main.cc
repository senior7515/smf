// Copyright (c) 2017 Alexander Gallego. All rights reserved.
//

#include <chrono>
#include <iostream>

#include <seastar/core/app-template.hh>
#include <seastar/core/distributed.hh>

#include "smf/histogram_seastar_utils.h"
#include "smf/load_channel.h"
#include "smf/load_generator.h"
#include "smf/log.h"
#include "smf/unique_histogram_adder.h"

// templates
#include "demo_service.smf.fb.h"

using client_t = smf_gen::demo::SmfStorageClient;
using load_gen_t = smf::load_generator<client_t>;
static const char *kPayload1Kbytes =
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

inline smf_gen::demo::payloadT
payload() {
  auto c1 = []() { return smf_gen::demo::c1(); };
  auto c2 = [&]() {
    smf_gen::demo::c2T ret;
    ret.x = std::make_unique<smf_gen::demo::c1>(c1());
    return ret;
  };
  auto c3 = [&]() {
    smf_gen::demo::c3T ret;
    ret.x = std::make_unique<smf_gen::demo::c2T>(c2());
    return ret;
  };
  auto c4 = [&]() {
    smf_gen::demo::c4T ret;
    ret.x = std::make_unique<smf_gen::demo::c3T>(c3());
    return ret;
  };
  auto c5 = [&]() {
    smf_gen::demo::c5T ret;
    ret.x = std::make_unique<smf_gen::demo::c4T>(c4());
    return ret;
  };
  smf_gen::demo::payloadT ret;
  ret.one = std::make_unique<smf_gen::demo::c1>(c1());
  ret.two = std::make_unique<smf_gen::demo::c2T>(c2());
  ret.three = std::make_unique<smf_gen::demo::c3T>(c3());
  ret.four = std::make_unique<smf_gen::demo::c4T>(c4());
  ret.five = std::make_unique<smf_gen::demo::c5T>(c5());
  return ret;
}

// This is just for the load generation.
// On normal apps you would just do
// client-><method_name>(params).then().then()
// as you would for normal seastar calls
// This example is just using the load generator to test performance
//
struct method_callback {
  seastar::future<>
  operator()(client_t *c, smf::rpc_envelope &&e) {
    return c->Get(std::move(e)).then([](auto ret) {
      return seastar::make_ready_future<>();
    });
  }
};

struct generator {
  smf::rpc_envelope
  operator()(const boost::program_options::variables_map &cfg) {
    uint32_t test = cfg["test-case"].as<uint32_t>();
    if (test == 1) {
      smf::rpc_typed_envelope<smf_gen::demo::Request> req;
      req.data->name = kPayload1Kbytes;
      return req.serialize_data();
    } else if (test == 2) {
      smf::rpc_typed_envelope<smf_gen::demo::ComplexRequest> req;
      req.data->data = std::make_unique<smf_gen::demo::payloadT>(payload());
      return req.serialize_data();
    }
    throw std::runtime_error("invalid test case, must be 1, or 2");
  }
};

void
cli_opts(boost::program_options::options_description_easy_init o) {
  namespace po = boost::program_options;

  o("ip", po::value<std::string>()->default_value("127.0.0.1"),
    "ip to connect to");

  o("port", po::value<uint16_t>()->default_value(20776), "port for service");

  o("test-case", po::value<uint32_t>()->default_value(1),
    "1: large payload, 2: complex struct");

  o("req-num", po::value<uint32_t>()->default_value(1000),
    "number of request per concurrenct connection");

  // currently these are sockets
  o("concurrency", po::value<uint32_t>()->default_value(10),
    "number of green threads per real thread (seastar::futures<>)");
}

int
main(int args, char **argv, char **env) {
  seastar::distributed<load_gen_t> load;
  seastar::app_template app;
  cli_opts(app.add_options());

  return app.run(args, argv, [&] {
    seastar::engine().at_exit([&] { return load.stop(); });
    auto &cfg = app.configuration();

    ::smf::load_generator_args largs(
      cfg["ip"].as<std::string>().c_str(), cfg["port"].as<uint16_t>(),
      cfg["req-num"].as<uint32_t>(), cfg["concurrency"].as<uint32_t>(),
      static_cast<uint64_t>(0.9 * seastar::memory::stats().total_memory()),
      smf::rpc::compression_flags::compression_flags_none, cfg);

    LOG_INFO("Load args: {}", largs);

    return load.start(std::move(largs))
      .then([&load] {
        LOG_INFO("Connecting to server");
        return load.invoke_on_all(&load_gen_t::connect);
      })
      .then([&load] {
        LOG_INFO("Benchmarking server");
        return load.invoke_on_all([](load_gen_t &server) {
          load_gen_t::generator_cb_t gen = generator{};
          load_gen_t::method_cb_t method = method_callback{};
          return server.benchmark(gen, method).then([](auto test) {
            LOG_INFO("Bench: {}", test);
            return seastar::make_ready_future<>();
          });
        });
      })
      .then([&load] {
        LOG_INFO("MapReducing stats");
        return load
          .map_reduce(smf::unique_histogram_adder(),
                      [](load_gen_t &shard) { return shard.copy_histogram(); })
          .then([](std::unique_ptr<smf::histogram> h) {
            LOG_INFO("Writing client histograms");
            return smf::histogram_seastar_utils::write("clients_latency.hgrm",
                                                       std::move(h));
          });
      })
      .then([] {
        LOG_INFO("Exiting");
        return seastar::make_ready_future<int>(0);
      });
  });
}
