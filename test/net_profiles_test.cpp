#include <assert.h>

#include <cstdlib>
#include <map>
#include <string>

#include "net_profiles.h"

namespace {

std::map<std::string, std::string> values;
bool writesFail = false;

}  // namespace

namespace store {

int32_t getInt(const char *key, int32_t fallback)
{
	const auto found = values.find(key);
	return found == values.end() ? fallback : (int32_t)strtol(found->second.c_str(), nullptr, 10);
}

bool setInt(const char *key, int32_t value)
{
	if (writesFail) {
		return false;
	}
	values[key] = std::to_string(value);
	return true;
}

String getString(const char *key, const char *fallback)
{
	const auto found = values.find(key);
	return found == values.end() ? String(fallback) : String(found->second);
}

bool setString(const char *key, const String &value)
{
	if (writesFail) {
		return false;
	}
	values[key] = value;
	return true;
}

bool remove(const char *key)
{
	return values.erase(key) > 0;
}

}  // namespace store

int main()
{
	values = {{"sys.ssid", "legacy"}, {"sys.pass", "old password"}};
	net_profiles::Profiles migrated;
	migrated.load();
	assert(migrated.count() == 1);
	assert(migrated.at(0)->ssid == "legacy");
	assert(migrated.at(0)->password == "old password");
	assert(values.count("sys.ssid") == 0);
	assert(values.count("sys.pass") == 0);

	assert(migrated.remember("open", ""));
	assert(migrated.remember("two", "password two"));
	assert(migrated.remember("three", "password three"));
	assert(migrated.remember("four", "password four"));
	assert(migrated.remember("five", "password five"));
	assert(migrated.count() == net_profiles::MAX_PROFILES);
	assert(migrated.at(0)->ssid == "five");
	assert(migrated.find("legacy") == -1);

	assert(migrated.remember("open", ""));
	assert(migrated.at(0)->ssid == "open");
	assert(migrated.at(0)->password.isEmpty());
	assert(values.count("sys.wifi") == 1);

	net_profiles::Profiles reloaded;
	reloaded.load();
	assert(reloaded.at(0)->ssid == "open");
	assert(reloaded.at(0)->password.isEmpty());
	assert(reloaded.forget("open"));
	assert(reloaded.find("open") == -1);
	assert(reloaded.count() == 3);

	// A failed replacement must leave the one durable blob untouched. Reloading
	// after the injected failure recovers the full old list and matching secrets.
	const std::string durable = values["sys.wifi"];
	writesFail = true;
	assert(!reloaded.remember("interrupted", "new password"));
	assert(values["sys.wifi"] == durable);
	net_profiles::Profiles afterFailure;
	afterFailure.load();
	assert(afterFailure.count() == 3);
	assert(afterFailure.at(0)->ssid == "five");
	assert(afterFailure.at(0)->password == "password five");

	values = {{"sys.ssid", "keep legacy"}, {"sys.pass", "keep password"}};
	net_profiles::Profiles failedMigration;
	failedMigration.load();
	assert(failedMigration.count() == 1);
	assert(failedMigration.at(0)->ssid == "keep legacy");
	assert(failedMigration.at(0)->password == "keep password");
	assert(values.count("sys.ssid") == 1);
	assert(values.count("sys.pass") == 1);
	return 0;
}
