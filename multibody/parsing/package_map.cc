#include "drake/multibody/parsing/package_map.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <utility>

#include <tinyxml2.h>

#include "drake/common/drake_assert.h"
#include "drake/common/drake_path.h"
#include "drake/common/drake_throw.h"
#include "drake/common/filesystem.h"
#include "drake/common/find_resource.h"
#include "drake/common/text_logging.h"
#include "drake/common/unused.h"

namespace drake {
namespace multibody {

using std::runtime_error;
using std::string;
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

PackageMap::PackageMap()
    : PackageMap{FindResourceOrThrow("drake/package.xml")} {}

PackageMap PackageMap::MakeEmpty() {
  return PackageMap(std::initializer_list<std::string>());
}

void PackageMap::Add(const string& package_name, const string& package_path) {
  if (!AddPackageIfNew(package_name, package_path)) {
    throw std::runtime_error(fmt::format(
        "PackageMap already contains package \"{}\" with path \"{}\" that "
        "conflicts with provided path \"{}\"",
        package_name, GetPath(package_name), package_path));
  }
}

void PackageMap::AddMap(const PackageMap& other_map) {
  for (const auto& [package_name, path] : other_map.map_) {
    Add(package_name, path);
  }
}

bool PackageMap::Contains(const string& package_name) const {
  return map_.find(package_name) != map_.end();
}

void PackageMap::Remove(const string& package_name) {
  if (map_.erase(package_name) == 0) {
    throw std::runtime_error(
        "Could not find and remove package://" + package_name + " from the "
        "search path.");
  }
}

int PackageMap::size() const {
  return map_.size();
}

std::vector<std::string> PackageMap::GetPackageNames() const {
  std::vector<std::string> package_names;
  package_names.reserve(map_.size());
  for (const auto& [package_name, path] : map_) {
    unused(path);
    package_names.push_back(package_name);
  }
  return package_names;
}

const string& PackageMap::GetPath(const string& package_name) const {
  DRAKE_DEMAND(Contains(package_name));
  return map_.at(package_name);
}

void PackageMap::PopulateFromFolder(const string& path) {
  DRAKE_DEMAND(!path.empty());
  CrawlForPackages(path);
}

void PackageMap::PopulateFromEnvironment(const string& environment_variable) {
  DRAKE_DEMAND(!environment_variable.empty());
  const char* const value = std::getenv(environment_variable.c_str());
  if (value == nullptr) {
    return;
  }
  std::istringstream iss{string(value)};
  string path;
  while (std::getline(iss, path, ':')) {
    if (!path.empty()) {
      CrawlForPackages(path);
    }
  }
}

namespace {

// Returns the package.xml file in the given directory, if any.
std::optional<filesystem::path> GetPackageXmlFile(const string& directory) {
  DRAKE_DEMAND(!directory.empty());
  filesystem::path filename = filesystem::path(directory) / "package.xml";
  if (filesystem::is_regular_file(filename)) {
    return filename;
  }
  return std::nullopt;
}

// Returns the parent directory of @p directory.
string GetParentDirectory(const string& directory) {
  DRAKE_DEMAND(!directory.empty());
  return filesystem::path(directory).parent_path().string();
}

// Parses the package.xml file specified by package_xml_file. Finds and returns
// the name of the package.
string GetPackageName(const string& package_xml_file) {
  DRAKE_DEMAND(!package_xml_file.empty());
  XMLDocument xml_doc;
  xml_doc.LoadFile(package_xml_file.data());
  if (xml_doc.ErrorID()) {
    throw runtime_error("package_map.cc: GetPackageName(): "
        "Failed to parse XML in file \"" + package_xml_file + "\".\n" +
        xml_doc.ErrorName());
  }

  XMLElement* package_node = xml_doc.FirstChildElement("package");
  if (!package_node) {
    throw runtime_error("package_map.cc: GetPackageName(): "
        "ERROR: XML file \"" + package_xml_file + "\" does not contain "
        "element <package>.");
  }

  XMLElement* name_node = package_node->FirstChildElement("name");
  if (!name_node) {
    throw runtime_error("package_map.cc: GetPackageName(): "
        "ERROR: <package> element does not contain element <name> "
        "(XML file \"" + package_xml_file + "\").");
  }

  // Throws an exception if the name node does not have any children.
  DRAKE_THROW_UNLESS(!name_node->NoChildren());
  const string package_name = name_node->FirstChild()->Value();
  DRAKE_THROW_UNLESS(package_name != "");
  return package_name;
}

}  // namespace

bool PackageMap::AddPackageIfNew(const string& package_name,
    const string& path) {
  DRAKE_DEMAND(!package_name.empty());
  DRAKE_DEMAND(!path.empty());
  // Don't overwrite entries in the map.
  if (!Contains(package_name)) {
    drake::log()->trace(
        "PackageMap: Adding package://{}: {}", package_name, path);
    if (!filesystem::is_directory(path)) {
      throw std::runtime_error(
          "Could not add package://" + package_name + " to the search path "
          "because directory " + path + " does not exist");
    }
    map_.insert(make_pair(package_name, path));
  } else {
    // Don't warn if we've found the same path with a different spelling.
    const string existing_path = GetPath(package_name);
    if (!filesystem::equivalent(existing_path, path)) {
      drake::log()->warn(
          "PackageMap is ignoring newly-found path \"{}\" for package \"{}\""
          " and will continue using the previously-known path at \"{}\".",
          path, package_name, existing_path);
      return false;
    }
  }
  return true;
}

void PackageMap::PopulateUpstreamToDrakeHelper(
    const string& directory,
    const string& stop_at_directory) {
  DRAKE_DEMAND(!directory.empty());

  // If we've reached the top, then stop searching.
  if (directory.length() <= stop_at_directory.length()) {
    return;
  }

  // If there is a new package.xml file, then add it.
  if (auto filename = GetPackageXmlFile(directory)) {
    const string package_name = GetPackageName(filename->string());
    AddPackageIfNew(package_name, directory);
  }

  // Continue searching in our parent directory.
  PopulateUpstreamToDrakeHelper(
      GetParentDirectory(directory), stop_at_directory);
}

void PackageMap::PopulateUpstreamToDrake(const string& model_file) {
  DRAKE_DEMAND(!model_file.empty());
  drake::log()->trace("PopulateUpstreamToDrake: {}", model_file);

  // Verify that the model_file names an URDF or SDF file.
  string extension = filesystem::path(model_file).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);
  if (extension != ".urdf" && extension != ".sdf") {
    throw std::runtime_error(fmt::format(
        "The file type '{}' is not supported for '{}'",
        extension, model_file));
  }
  const string model_dir = filesystem::path(model_file).parent_path().string();

  // Bail out if we can't determine the drake root.
  const std::optional<string> maybe_drake_path = MaybeGetDrakePath();
  if (!maybe_drake_path) {
    drake::log()->trace("  Could not determine drake_path");
    return;
  }
  // Bail out if the model file is not part of Drake.
  const string& drake_path = *maybe_drake_path;
  auto iter = std::mismatch(drake_path.begin(), drake_path.end(),
                            model_dir.begin());
  if (iter.first != drake_path.end()) {
    drake::log()->trace("  drake_path was not a prefix of model_dir.");
    return;
  }

  // Search the directory containing the model_file and "upstream".
  PopulateUpstreamToDrakeHelper(model_dir, drake_path);
}

PackageMap::PackageMap(std::initializer_list<std::string> manifest_paths) {
  for (const auto& manifest_path : manifest_paths) {
    AddPackageXml(manifest_path);
  }
}

void PackageMap::CrawlForPackages(const string& path) {
  DRAKE_DEMAND(!path.empty());
  std::error_code ec;
  filesystem::directory_iterator iter(
      filesystem::path(path).lexically_normal(), ec);
  if (ec) {
    log()->warn("Unable to open directory: {}", path);
    return;
  }
  for (const auto& entry : iter) {
    if (entry.is_directory()) {
      const string filename = entry.path().filename().string();
      // Skips hidden directories (including "." and "..").
      if (filename.at(0) == '.') {
        continue;
      }
      CrawlForPackages(entry.path().string());
    } else if (entry.path().filename().string() == "package.xml") {
      const string package_name = GetPackageName(entry.path().string());
      const string package_path = entry.path().parent_path().string();
      AddPackageIfNew(package_name, package_path + "/");
    }
  }
}

void PackageMap::AddPackageXml(const string& filename) {
  const string package_name = GetPackageName(filename);
  const string package_path = GetParentDirectory(filename);
  Add(package_name, package_path);
}

std::ostream& operator<<(std::ostream& out, const PackageMap& package_map) {
    out << "PackageMap:\n";
    if (package_map.size() == 0)
      out << "  [EMPTY!]\n";
    for (const auto& entry : package_map.map_) {
      out << "  - " << entry.first << ": " << entry.second << "\n";
    }
    return out;
}

}  // namespace multibody
}  // namespace drake
