#pragma once
#include <sqlite3.h>
#include <filesystem>
#include <string>
#include <map>
#include <atomic>
#include <expected>
#include <fstream>


#include <SQLiteCpp/SQLiteCpp.h>

#include <tgbm/utils/scope_exit.hpp>
#include <tgbm/api/Integer.hpp>
#include <tgbm/logger.hpp>

template<typename T>
std::optional<T> GetQueryOne(SQLite::Statement &que) {
  if (!que.executeStep()) {
    return std::nullopt;
  } else {
    T ans;
    ans = que.getColumn(0);
    if (que.executeStep()) {
      assert(false);
    }
    return ans;
  }
}

std::string EscapedStr(std::string_view sv);

int traceCallback(unsigned mask, void*, void* pStmt, void*);

class Database {
private:
  using fspath = std::filesystem::path;
public:
  static inline const fspath kDatabaseDir = [] -> fspath {
    if (const char *env = std::getenv("ACT_CHECKER_DATABASE_DIR"); env != nullptr) {
      return env;
    } else {
      return std::filesystem::current_path();
    }
  } ();

  Database();

  static constexpr std::string_view kActivityFileName = "acitity.csv";
  static constexpr std::string_view kOwnerFileName = "owner";
  static inline std::atomic<uint64_t> cur_coll_cnt = 0;
  static_assert(decltype(cur_coll_cnt)::is_always_lock_free);

  std::expected<std::filesystem::path, std::string_view> GetActivityCheckList(int64_t check_id,
                                                                              tgbm::api::Integer asker) const;

  // returns message to user
  std::string_view AddActivity(int64_t check_id, int64_t userId, std::string_view fullName,
                               std::chrono::system_clock::time_point checkInDate);

  // returns check id
  int64_t AddActivityCheck(int64_t owner);

 private:
  SQLite::Database db;
  SQLite::Statement add_activity_check_sttmnt;
  mutable SQLite::Statement get_owner_by_id;
  SQLite::Statement add_activity;
};


Database& GetDb();
