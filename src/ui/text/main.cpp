#include <ios>
#include <iostream>
#include <iomanip>
#include <string>
#include <exception>
#include <stdexcept>
#include <limits>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/filesystem/path.hpp>

#include <medusa/configuration.hpp>
#include <medusa/address.hpp>
#include <medusa/medusa.hpp>
#include <medusa/document.hpp>
#include <medusa/memory_area.hpp>
#include <medusa/log.hpp>
#include <medusa/event_handler.hpp>
#include <medusa/disassembly_view.hpp>
#include <medusa/view.hpp>
#include <medusa/module.hpp>
#include <medusa/user_configuration.hpp>

MEDUSA_NAMESPACE_USE

class TextFullDisassemblyView : public FullDisassemblyView
{
public:
  TextFullDisassemblyView(Medusa& rCore, u32 FormatFlags, u32 Width, u32 Height, Address const& rAddress)
  : FullDisassemblyView(rCore, FormatFlags, Width, Height, rAddress)
  {}

  void Print(void)
  {
    std::cout << m_PrintData.GetTexts() << std::endl;
  }

};

std::ostream& operator<<(std::ostream& out, std::pair<u32, std::string> const& p)
{
  out << p.second;
  return out;
}

template<typename Type, typename Container>
class AskFor
{
public:
  Type operator()(Container const& c)
  {
    if (c.empty())
      throw std::runtime_error("Nothing to ask!");

    while (true)
    {
      size_t Count = 0;
      for (typename Container::const_iterator i = c.begin(); i != c.end(); ++i)
      {
        std::cout << Count << " " << (*i)->GetName() << std::endl;
        Count++;
      }
      size_t Input;
      std::cin >> Input;

      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

      if (Input < c.size())
        return c[Input];
    }
  }
};

struct AskForConfiguration : public boost::static_visitor<>
{
  AskForConfiguration(ConfigurationModel& rCfgMdl) : m_rCfgMdl(rCfgMdl) {}

  ConfigurationModel& m_rCfgMdl;

  void operator()(ConfigurationModel::NamedBool const& rBool) const
  {
    std::cout << rBool.GetName() << " " << rBool.GetValue() << std::endl;
    std::cout << "true/false" << std::endl;

    while (true)
    {
      u32 Choose;
      std::string Result;

      std::getline(std::cin, Result, '\n');

      if (Result.empty()) return;

      if (Result == "false" || Result == "true")
      {
        m_rCfgMdl.SetEnum(rBool.GetName(), !!(Result == "true"));
        return;
      }

      std::istringstream iss(Result);
      if (!(iss >> Choose)) continue;

      if (Choose == 0 || Choose == 1)
      {
        m_rCfgMdl.SetEnum(rBool.GetName(), Choose ? true : false);
        return;
      }
    }
  }

  void operator()(ConfigurationModel::NamedEnum const& rEnum) const
  {
    std::cout << std::dec;
    std::cout << "ENUM TYPE: " << rEnum.GetName() << std::endl;
    for (Configuration::Enum::const_iterator It = rEnum.GetConfigurationValue().begin();
      It != rEnum.GetConfigurationValue().end(); ++It)
    {
      if (It->second == m_rCfgMdl.GetEnum(rEnum.GetName()))
        std::cout << "* ";
      else
        std::cout << "  ";
      std::cout << It->first << ": " << It->second << std::endl;
    }

    while (true)
    {
      u32 Choose;
      std::string Result;

      std::getline(std::cin, Result, '\n');

      if (Result.empty()) return;

      std::istringstream iss(Result);
      if (!(iss >> Choose)) continue;

      for (Configuration::Enum::const_iterator It = rEnum.GetConfigurationValue().begin();
        It != rEnum.GetConfigurationValue().end(); ++It)
        if (It->second == Choose)
        {
          m_rCfgMdl.SetEnum(rEnum.GetName(), Choose ? true : false);
          return;
        }
    }
  }
};

void TextLog(std::string const & rMsg)
{
  std::cout << rMsg << std::flush;
}

int main(int argc, char **argv)
{
  std::cout.sync_with_stdio(false);
  std::wcout.sync_with_stdio(false);
  boost::filesystem::path file_path;
  boost::filesystem::path mod_path;
  boost::filesystem::path db_path;
  Log::SetLog(TextLog);

  UserConfiguration usr_cfg;
  std::string mod_path_opt;
  if (!usr_cfg.GetOption("core.modules_path", mod_path_opt))
    mod_path = ".";
  else
    mod_path = mod_path_opt;

  try
  {
    if (argc != 3)
      return 0;
    file_path = argv[1];
    db_path = argv[2];

    Log::Write("ui_text") << "Analyzing the following file: \""         << file_path.string() << "\"" << LogEnd;
    Log::Write("ui_text") << "Database will be saved to the file: \""   << db_path.string()   << "\"" << LogEnd;
    Log::Write("ui_text") << "Using the following path for modules: \"" << mod_path.string()  << "\"" << LogEnd;

    Medusa m;
    if (!m.NewDocument(
      file_path,
      [&](boost::filesystem::path& rDatabasePath, std::list<Medusa::Filter> const& rExtensionFilter)
      {
        rDatabasePath = db_path;
        return true;
      },
      [&](
        BinaryStream::SharedPtr spBinStrm,
        Database::SharedPtr& rspDatabase,
        Loader::SharedPtr& rspLoader,
        Architecture::VectorSharedPtr& rspArchitectures,
        OperatingSystem::SharedPtr& rspOperatingSystem
      )
      {
      auto& mod_mgr = ModuleManager::Instance();

      mod_mgr.UnloadModules();
      mod_mgr.LoadModules(mod_path, *spBinStrm);

      if (mod_mgr.GetLoaders().empty())
      {
        Log::Write("ui_text") << "Not loader available" << LogEnd;
        return false;
      }

      std::cout << "Choose a executable format:" << std::endl;
      AskFor<Loader::VectorSharedPtr::value_type, Loader::VectorSharedPtr> AskForLoader;
      Loader::VectorSharedPtr::value_type ldr = AskForLoader(mod_mgr.GetLoaders());
      std::cout << "Interpreting executable format using \"" << ldr->GetName() << "\"..." << std::endl;
      std::cout << std::endl;
      rspLoader = ldr;

      auto archs = mod_mgr.GetArchitectures();
      ldr->FilterAndConfigureArchitectures(archs);
      if (archs.empty())
        throw std::runtime_error("no architecture available");

      rspArchitectures = archs;

      auto os = mod_mgr.GetOperatingSystem(ldr, archs.front());
      rspOperatingSystem = os;

      std::cout << std::endl;

      ConfigurationModel CfgMdl;

      std::cout << "Configuration:" << std::endl;
      for (ConfigurationModel::ConstIterator itCfg = CfgMdl.Begin(), itEnd = CfgMdl.End(); itCfg != CfgMdl.End(); ++itCfg)
        boost::apply_visitor(AskForConfiguration(CfgMdl), itCfg->second);

      AskFor<Database::VectorSharedPtr::value_type, Database::VectorSharedPtr> AskForDb;
      auto db = AskForDb(mod_mgr.GetDatabases());
      rspDatabase = db;
      return true;
      },
      [](){ std::cout << "Analyzing..." << std::endl; return true; },
      [](){ return true; }))
    throw std::runtime_error("failed to create new document");

    m.WaitForTasks();

    int step = 100;
    TextFullDisassemblyView tfdv(m, FormatDisassembly::ShowAddress | FormatDisassembly::AddSpaceBeforeXref, 80, step, m.GetDocument().GetStartAddress());
    do tfdv.Print();
    while (tfdv.MoveView(0, step));
  }
  catch (std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  catch (Exception& e)
  {
    std::wcerr << e.What() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
