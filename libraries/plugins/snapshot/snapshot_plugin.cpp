#include <steemit/snapshot/snapshot_plugin.hpp>
#include <steemit/snapshot/snapshot_state.hpp>

#include <steemit/chain/index.hpp>
#include <steemit/chain/account_object.hpp>
#include <steemit/chain/operation_notification.hpp>

#include <steemit/account_by_key/account_by_key_plugin.hpp>
#include <steemit/follow/follow_api.hpp>

#include <boost/iostreams/device/mapped_file.hpp>

namespace steemit {
    namespace plugin {
        namespace snapshot {
            namespace detail {
                class snapshot_plugin_impl {
                public:
                    snapshot_plugin_impl(snapshot_plugin &plugin)
                            : self(plugin) {

                    }

                    steemit::chain::database &database() {
                        return self.database();
                    }

                    void pre_operation(const chain::operation_notification &op_obj);

                    void post_operation(const chain::operation_notification &op_obj);

                    void update_key_lookup(const chain::account_authority_object &a);

                    snapshot_plugin &self;
                };

                struct pre_operation_visitor {
                    snapshot_plugin &_plugin;

                    pre_operation_visitor(snapshot_plugin &plugin)
                            : _plugin(plugin) {
                    }

                    typedef void result_type;

                    template<typename T>
                    void operator()(const T &) const {
                    }
                };

                struct post_operation_visitor {
                    snapshot_plugin &_plugin;

                    post_operation_visitor(snapshot_plugin &plugin)
                            : _plugin(plugin) {
                    }

                    typedef void result_type;

                    template<typename T>
                    void operator()(const T &) const {
                    }

                    void operator()(const chain::hardfork_operation &op) const {
                        if (op.hardfork_id == STEEMIT_HARDFORK_0_17) {
                            auto &db = _plugin.database();

                            if (_plugin.get_loaded_snapshots().right.find("75b7287ca7d39fcfb742ba184f6e2f6debb49cf70f0c7e3dcfffe45b518ee64c") !=
                                _plugin.get_loaded_snapshots().right.end()) {
                                snapshot_state snapshot = fc::json::from_file(fc::path(_plugin.get_loaded_snapshots().right.at("75b7287ca7d39fcfb742ba184f6e2f6debb49cf70f0c7e3dcfffe45b518ee64c"))).as<snapshot_state>();
                                for (account_summary &account : snapshot.accounts) {
                                    db.modify(*db.find_account(account.name), [&](chain::account_object &a) {
                                        std::size_t position = a.json_metadata.find("created_at: 'GENESIS'");
                                        if (position != std::string::npos) {
                                            a.json_metadata.erase(a.json_metadata.find("created_at: 'GENESIS'"),
                                                    a.json_metadata.find("created_at: 'GENESIS'") +
                                                    std::string("created_at: 'GENESIS'").length());
                                            a.json_metadata.insert(
                                                    a.json_metadata.find_first_of('{') +
                                                    1, "\"created_at\": \"GENESIS\",");
                                        }
                                    });
                                }
                            }
                        }
                    }
                };

                void snapshot_plugin_impl::pre_operation(const chain::operation_notification &note) {
                    note.op.visit(pre_operation_visitor(self));
                }

                void snapshot_plugin_impl::post_operation(const chain::operation_notification &note) {
                    note.op.visit(post_operation_visitor(self));
                }

                void snapshot_plugin_impl::update_key_lookup(const chain::account_authority_object &a) {
                    try {
                        self.application->get_plugin<account_by_key::account_by_key_plugin>(ACCOUNT_BY_KEY_PLUGIN_NAME)->update_key_lookup(a);
                    } catch (fc::assert_exception) {
                        ilog("Account by key plugin not loaded");
                    }
                }
            }

            snapshot_plugin::snapshot_plugin(steemit::app::application *app)
                    : plugin(app),
                      impl(new detail::snapshot_plugin_impl(*this)) {
            }

            snapshot_plugin::~snapshot_plugin() {
            }

            std::string snapshot_plugin::plugin_name() const {
                return SNAPSHOT_PLUGIN_NAME;
            }

            void snapshot_plugin::plugin_initialize(const boost::program_options::variables_map &options) {
                ilog("Initializing snapshot plugin");

                this->options = options;

                if (!options.count("snapshot-file")) {
                    this->options.insert({"snapshot-file", boost::program_options::variable_value("snapshot5392323.json", false)});
                    this->options.notify();
                }

                chain::database &db = database();

                const vector<string> snapshots = this->options["snapshot-file"].as<vector<string>>();

                for (const vector<string>::value_type &iterator : snapshots) {
                    FC_ASSERT(fc::exists(iterator), "Snapshot file '${file}' was not found.", ("file", iterator));

                    ilog("Loading snapshot from ${s}", ("s", iterator));

                    std::string snapshot_hash(fc::sha256(boost::iostreams::mapped_file_source(fc::path(iterator).string()).data()));

                    snapshot_state snapshot = fc::json::from_file(fc::path(iterator)).as<snapshot_state>();
                    for (account_summary &account : snapshot.accounts) {
                        db.create<chain::account_object>([&](chain::account_object &a) {
                            a.name = account.name;
                            a.memo_key = account.keys.memo_key;

                            if (snapshot_hash ==
                                "75b7287ca7d39fcfb742ba184f6e2f6debb49cf70f0c7e3dcfffe45b518ee64c") {
                                a.json_metadata = "{created_at: 'GENESIS'}";
                                a.recovery_account = STEEMIT_INIT_MINER_NAME;
                            } else {
                                a.json_metadata = account.json_metadata.data();
                                a.recovery_account = account.recovery_account;
                            }
                        });

                        impl->update_key_lookup(db.create<chain::account_authority_object>([&](chain::account_authority_object &auth) {
                            auth.account = account.name;
                            auth.owner.weight_threshold = 1;
                            auth.owner = account.keys.owner_key;
                            auth.active = account.keys.active_key;
                            auth.posting = account.keys.posting_key;
                        }));
                    }

                    loaded_snapshots.insert({iterator,
                                             fc::sha256(boost::iostreams::mapped_file_source(fc::path(iterator).string()).data())
                    });
                }
            }

            void snapshot_plugin::plugin_set_program_options(boost::program_options::options_description &command_line_options, boost::program_options::options_description &config_file_options) {
                command_line_options.add_options()
                        ("snapshot-file", boost::program_options::value<string>()->composing()->multitoken(), "Snapshot files to load");
                config_file_options.
                        add(command_line_options);
            }

            void snapshot_plugin::plugin_startup() {

            }

            const boost::bimap<string, string> &snapshot_plugin::get_loaded_snapshots() const {
                return loaded_snapshots;
            }
        }
    }
}

/**
 * The STEEMIT_DEFINE_PLUGIN() macro will define a steemit::plugin::create_hello_api_plugin()
 * factory method which is expected by the manifest.
 */

STEEMIT_DEFINE_PLUGIN(snapshot, steemit::plugin::snapshot::snapshot_plugin)