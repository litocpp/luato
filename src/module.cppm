export module luato:module_loader;

export import :error;

using namespace rstd::prelude;

export namespace luato {

struct LuaModuleSource {
  String logical_name;
  String identity;
  String display_path;
  Vec<u8> bytes;
};

struct ModuleRequest {
  String importer_identity;
  String importer_path;
  String requested;
};

using ModuleResolver = dyn<FnMut<Result<LuaModuleSource>(ModuleRequest)>>;

class ModuleResolverSpec {
public:
  ModuleResolverSpec(ModuleResolverSpec &&) noexcept = default;
  auto operator=(ModuleResolverSpec &&) noexcept
      -> ModuleResolverSpec & = default;

  template <typename Resolver>
  static auto make(Resolver &&resolver) -> ModuleResolverSpec {
    return ModuleResolverSpec(
        Box<ModuleResolver>::make(rstd::forward<Resolver>(resolver)));
  }

private:
  explicit ModuleResolverSpec(Box<ModuleResolver> resolver)
      : resolver_(rstd::move(resolver)) {}

  Box<ModuleResolver> resolver_;

  friend class State;
};

struct LoadedModule {
  String logical_name;
  String identity;
  String display_path;

  auto clone() const -> LoadedModule {
    return LoadedModule{logical_name.clone(), identity.clone(),
                        display_path.clone()};
  }
};

} // namespace luato
