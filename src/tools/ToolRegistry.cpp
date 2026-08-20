#include "tools/ToolRegistry.hpp"
#include <stdexcept>

void ToolRegistry::registry_tool(const std::string &name,
                                 const std::string &description,
                                 const json &parameters,
                                 ToolFunction func)
{
    definitions_[name] = {name, description, parameters};
    functions_[name] = std::move(func);
}

void ToolRegistry::registry_tools(const std::vector<ToolDefinition> &defs,
                                  const std::vector<ToolFunction> &funcs)
{
    if (defs.size() != funcs.size())
    {
        throw std::invalid_argument("defs and func size mismatch");
    }
    for (size_t i = 0; i < defs.size(); ++i)
    {
        definitions_[defs[i].name] = defs[i];
        functions_[defs[i].name] = funcs[i];
    }
}

std::vector<ToolDefinition> ToolRegistry::get_all_definitions() const
{
    std::vector<ToolDefinition> res;
    res.reserve(definitions_.size());
    for (const auto &[name, def] : definitions_)
    {
        res.push_back(def);
    }
    return res;
}

bool ToolRegistry::exists(const std::string &name) const
{
    return functions_.find(name) != functions_.end();
}

std::string ToolRegistry::execute(const std::string &name, const json &args)
{
    auto it = functions_.find(name);
    if (it == functions_.end())
    {
        return "Error: Tool '" + name + "' not found";
    }
    try
    {
        return it->second(args);
    }
    catch (const std::exception &e)
    {
        return "Error executing tool: " + std::string(e.what());
    }
}