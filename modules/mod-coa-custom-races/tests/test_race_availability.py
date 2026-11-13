import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='coa-race-policy-') as temporary:
        directory = Path(temporary)
        (directory / 'Config.h').write_text('''
#pragma once
#include <string>
#include <type_traits>
#include <unordered_map>
struct Config
{
    std::unordered_map<std::string, std::string> Values;
    template<class T> T GetOption(std::string const& key, T fallback)
    {
        auto it = Values.find(key);
        if (it == Values.end()) return fallback;
        if constexpr (std::is_same_v<T, bool>) return it->second == "1";
        else return it->second;
    }
};
inline Config ConfigInstance;
inline Config* sConfigMgr = &ConfigInstance;
''')
        (directory / 'DBCStores.h').write_text('''
#pragma once
#include <cstdint>
#include <set>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
struct Store
{
    bool Loaded = false;
    std::set<uint8> Known = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 82, 83, 97};
    uint32 GetNumRows() const { return Loaded ? 98 : 0; }
    void const* LookupEntry(uint8 race) const { return Known.count(race) ? this : nullptr; }
};
inline Store sChrRacesStore;
''')
        (directory / 'Log.h').write_text('''
#pragma once
#define LOG_INFO(...) do {} while (false)
#define LOG_ERROR(...) do {} while (false)
''')
        (directory / 'CryptoHash.h').write_text('''
#pragma once
#include <array>
#include <string_view>
namespace Acore::Crypto
{
    struct SHA256
    {
        void UpdateData(std::string_view) { }
        void Finalize() { }
        std::array<unsigned char, 32> GetDigest() const { return {}; }
    };
}
''')
        (directory / 'dbc_races').mkdir()
        (directory / 'dbc_races/ChrRaces.dbc').write_bytes(b'synthetic catalog bytes')
        (directory / 'ScriptMgr.h').write_text('''
#pragma once
#include "DBCStores.h"
#include <initializer_list>
enum {WORLDHOOK_ON_BEFORE_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, ACCOUNTHOOK_CAN_ACCOUNT_CREATE_CHARACTER};
struct WorldScript
{
    WorldScript(char const*, std::initializer_list<int>) { }
    virtual void OnBeforeConfigLoad(bool) { }
    virtual void OnStartup() { }
    virtual ~WorldScript() = default;
};
struct AccountScript
{
    AccountScript(char const*, std::initializer_list<int>) { }
    virtual bool CanAccountCreateCharacter(uint32, uint8, uint8) { return true; }
    virtual ~AccountScript() = default;
};
''')
        source = directory / 'test.cpp'
        source.write_text('''
#include <cstdlib>
#include <iostream>
#include "''' + (ROOT / 'src/mod_coa_custom_races.cpp').as_posix() + '''"
void AddCoAHideArmorScripts() { }
void Require(bool ok)
{
    if (!ok) std::abort();
}
int main(int, char** argv)
{
    using namespace CoARaceAvailability;
    Require(NormalizeCatalogHash("").has_value());
    Require(NormalizeCatalogHash(std::string(64, 'A')).value() == std::string(64, 'a'));
    Require(!NormalizeCatalogHash("bad"));
    for (auto text : {"", " ", "82", "82, 83,97", "9"}) Require(Parse(text).has_value());
    for (auto text : {",", "82,", ",82", "82,,83", "82,82", "0", "256", "-1", "+82",
        "82x", "1-82", "1", "2", "3", "4", "5", "6", "7", "8", "10", "11", "99999999999999"})
        Require(!Parse(text));
    CoACustomRacesConfig world;
    CoACustomRacesAccount account;
    world.OnBeforeConfigLoad(false);
    Require(account.CanAccountCreateCharacter(1, 1, 12));
    Require(!account.CanAccountCreateCharacter(1, 82, 12));
    ConfigInstance.Values["CoACustomRaces.Enable"] = "1";
    ConfigInstance.Values["CoACustomRaces.DisabledRaceIds"] = "82,83";
    world.OnBeforeConfigLoad(false);
    Require(!account.CanAccountCreateCharacter(1, 97, 12));
    sChrRacesStore.Loaded = true;
    world.OnStartup();
    Require(!account.CanAccountCreateCharacter(1, 82, 12));
    Require(!account.CanAccountCreateCharacter(1, 83, 12));
    Require(account.CanAccountCreateCharacter(1, 97, 12));
    for (auto race : {1,2,3,4,5,6,7,8,10,11})
        Require(account.CanAccountCreateCharacter(1, static_cast<uint8>(race), 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceIds"] = "97,250";
    world.OnBeforeConfigLoad(true);
    Require(!account.CanAccountCreateCharacter(1, 82, 12));
    Require(account.CanAccountCreateCharacter(1, 97, 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceIds"] = "97,junk";
    world.OnBeforeConfigLoad(true);
    Require(!account.CanAccountCreateCharacter(1, 82, 12));
    Require(account.CanAccountCreateCharacter(1, 97, 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceIds"] = "";
    world.OnBeforeConfigLoad(true);
    Require(account.CanAccountCreateCharacter(1, 82, 12));
    ConfigInstance.Values["DataDir"] = argv[1];
    ConfigInstance.Values["CoACustomRaces.DisabledRaceIds"] = "97";
    ConfigInstance.Values["CoACustomRaces.DisabledRaceCatalogSha256"] = std::string(64, '1');
    world.OnBeforeConfigLoad(true);
    Require(account.CanAccountCreateCharacter(1, 97, 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceCatalogSha256"] = std::string(64, '0');
    world.OnBeforeConfigLoad(true);
    Require(!account.CanAccountCreateCharacter(1, 97, 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceCatalogSha256"] = "bad";
    world.OnBeforeConfigLoad(true);
    Require(!account.CanAccountCreateCharacter(1, 97, 12));
    ConfigInstance.Values["CoACustomRaces.DisabledRaceCatalogSha256"] = "";
    ConfigInstance.Values["CoACustomRaces.Enable"] = "0";
    world.OnBeforeConfigLoad(true);
    Require(!account.CanAccountCreateCharacter(1, 82, 12));
    Require(account.CanAccountCreateCharacter(1, 1, 12));
    std::cout << "PASS: production module parser, lifecycle, stock races, rejection, re-enable and atomic invalid-config handling";
}
''')
        executable = directory / 'policy.exe'
        if os.environ.get('VCToolsInstallDir'):
            compiler = Path(os.environ['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
            command = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX',
                       '/I' + str(directory), str(source), '/Fe' + str(executable)]
        else:
            compiler = shutil.which('g++')
            if not compiler:
                raise RuntimeError('Set VCToolsInstallDir or install a C++17 compiler')
            command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(directory),
                       str(source), '-o', str(executable)]
        subprocess.run(command, cwd=directory, check=True, timeout=60)
        subprocess.run([str(executable), str(directory)], check=True, timeout=15)


if __name__ == '__main__':
    main()
