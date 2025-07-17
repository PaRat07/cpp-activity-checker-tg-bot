#pragma once
#include <sqlite3.h>
#include <filesystem>
#include <string>
#include <map>
#include <atomic>
#include <expected>
#include <fstream>

#include <SQLiteCpp/SQLiteCpp.h>

#include <tgbm/api/Integer.hpp>
#include <tgbm/logger.hpp>

template<size_t N>
void TemplateIndicies(auto &&f) {
  [&f] <size_t... Inds> (std::index_sequence<Inds...>) {
    (f(std::integral_constant<size_t, Inds>{}), ...);
  } (std::make_index_sequence<N>{});
}

template<typename T>
std::vector<T> GetQueryResAll(SQLite::Statement &que) {
  std::vector<T> ans;
  while (que.executeStep()) {
    ans.emplace_back();
    TemplateIndicies<std::tuple_size_v<T>>([&tup = ans.back(), &que] <size_t Ind> (std::integral_constant<size_t, Ind>) {
      std::get<Ind>(tup) = que.getColumn(Ind);
    });
  }
  return ans;
}

// template<typename T> requires (requires { typename std::tuple_size<T>; })
// std::optional<T> GetQueryOne(SQLite::Statement &que) {
//   if (!que.executeStep()) {
//     return std::nullopt;
//   } else {
//     T ans;
//     TemplateIndicies<std::tuple_size_v<T>>([&tup = ans.back(), &que] <size_t Ind> (std::integral_constant<size_t, Ind>) {
//       std::get<Ind>(tup) = que.getColumn(Ind);
//     });
//     if (!que.executeStep()) {
//       assert(false);
//     }
//     return ans;
//   }
// }

template<typename T>// requires (!requires { typename std::tuple_size<T>; })
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

std::string EscapedStr(std::string_view sv) {
  std::string ans;
  ans.push_back('"');
  for (char i : sv) {
    switch (i) {
      case '\n':
      case '"':
      case ',':
        ans.push_back('"');
      default:
        ans.push_back(i);
    }
  }
  ans.push_back('"');
  return ans;
}
static void sqliteLogCallback(void* /*pUserData*/, int errCode, const char* msg)
{
  // You can filter on errCode if you want only warnings, only errors, etc.
  TGBM_LOG_DEBUG("[SQLite][code={}]: {}", errCode, msg);
}

static int traceCallback(
    unsigned mask,
    void*    /*pCtx*/,
    void*    pStmt,
    void*    /*pX*/
){
  auto* stmt = static_cast<sqlite3_stmt*>(pStmt);
  const char* sql = sqlite3_sql(stmt);
  if (sql) {
    TGBM_LOG_DEBUG("[SQLite][code={}]: {}", mask, sql);

    // This returns a NEW malloc’d string with parameters expanded
    char* expanded = sqlite3_expanded_sql(stmt);
    if (expanded) {
    TGBM_LOG_DEBUG("[SQLite][code={}]: {}", mask, expanded);
      sqlite3_free(expanded);
    }
  }
  return 0;
}

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

  Database()
    : db([] -> SQLite::Database {
          fspath db_file = kDatabaseDir / "activity_checker_db.sq3";

      // sqlite3_config(SQLITE_CONFIG_LOG, sqliteLogCallback, nullptr);

          auto db = SQLite::Database(db_file, SQLite::OPEN_CREATE | SQLite::OPEN_READWRITE);

          db.exec("PRAGMA journal_mode = WAL;");
      sqlite3_trace_v2(
     db.getHandle(),
     SQLITE_TRACE_STMT,     // trace every statement
     &traceCallback,
     nullptr
 );
          db.exec("CREATE TABLE IF NOT EXISTS activity (activity_check_id INTEGER, participant_id INTEGER, UNIQUE(activity_check_id, participant_id))");
          db.exec("CREATE TABLE IF NOT EXISTS activity_check (id INTEGER PRIMARY KEY AUTOINCREMENT, owner INTEGER)");
          return db;
        } ()),
      add_activity_check_sttmnt(db, "INSERT OR IGNORE INTO activity_check (owner) VALUES (?)"),
      get_owner_by_id(db, "SELECT owner FROM activity_check WHERE id = ?"),
      add_activity(db, "INSERT OR IGNORE INTO activity (activity_check_id, participant_id) VALUES (?, ?)"){
  }

  static constexpr std::string_view kActivityFileName = "acitity.csv";
  static constexpr std::string_view kOwnerFileName = "owner";
  static inline std::atomic<uint64_t> cur_coll_cnt = 0;
  static_assert(decltype(cur_coll_cnt)::is_always_lock_free);

  std::expected<std::filesystem::path, std::string_view> GetActivityCheckList(int64_t check_id, tgbm::api::Integer asker) const {
    fspath act_check_dir = kDatabaseDir / std::to_string(check_id);
    // if (!std::filesystem::exists(act_check_dir)) {
    //   TGBM_LOG_INFO("access error: {} tried to access non-existing activity-check", asker);
    //   return std::unexpected("ОШИБКА: данной проверки активности не существует.");
    // }
    get_owner_by_id.bind(1, check_id);
    // i will not use it anymore in this func invocation
    on_scope_exit {
      get_owner_by_id.reset();
    };
    uint64_t real_owner;
    // if activity check in database it is in fs
    if (auto real_owner_opt = GetQueryOne<int>(get_owner_by_id); real_owner_opt.has_value()) {
      real_owner = *real_owner_opt;
    } else {
      TGBM_LOG_INFO("access error: {} tried to access non-existing activity-check", asker);
      return std::unexpected("ОШИБКА: данной проверки активности не существует.");
    }

    if (real_owner != asker.value) {
      TGBM_LOG_INFO("access error: {} tried to access {} activity check(owner: {})", asker, check_id, real_owner);
      return std::unexpected("ОШИБКА: вы не имеете доступа к данной проверке активности.");
    }
    assert(std::filesystem::exists(act_check_dir / kActivityFileName));
    return act_check_dir / kActivityFileName;
  }

  // returns message to user
  std::string_view AddActivity(int64_t check_id, int64_t userId, std::string_view fullName, std::chrono::system_clock::time_point checkInDate) {
    add_activity.bind(1, check_id);
    add_activity.bind(2, userId);
    // i will not use it anymore in this func invocation
    on_scope_exit {
      add_activity.reset();
    };
    if (add_activity.exec() > 0) {
      fspath act_check_dir = kDatabaseDir / std::to_string(check_id);
      std::string to_append = fmt::format("{},{},{:%Y-%m-%dT%H:%M:%S}\n", userId, EscapedStr(fullName), checkInDate);
      // TODO check if std::ofstream::write is atomic
      std::ofstream(act_check_dir / kActivityFileName, std::ios::app).write(to_append.data(), to_append.size());
      return "Спасибо, ваша активность учтена";
    } else {
      TGBM_LOG_INFO("user {} tried to register to {} repeatedly", userId, check_id);
      return "Спасибо, но ваша активность уже была учтена ранее";
    }
  }

  // returns check id
  int64_t AddActivityCheck(int64_t owner) {
    // required for consistency of fs and db
    SQLite::Transaction trx(db);
    add_activity_check_sttmnt.bind(1, owner);
    add_activity_check_sttmnt.exec();
    add_activity_check_sttmnt.reset();
    int64_t check_id = db.getLastInsertRowid();
    fspath act_check_dir = kDatabaseDir / std::to_string(check_id);
    std::filesystem::create_directory(act_check_dir);
    static constexpr std::string_view kCsvHeaders = "userId,fullName,checkInDate\n";
    // TODO check if std::ofstream::write is atomic
    // std::ios::trunk is for cases, when
    std::ofstream(act_check_dir / kActivityFileName, std::ios::trunc).write(kCsvHeaders.data(), kCsvHeaders.size());
    // if program fail here activity check is in fs, but not it db
    trx.commit();
    return check_id;
  }

private:
  SQLite::Database db;
  SQLite::Statement add_activity_check_sttmnt;
  mutable SQLite::Statement get_owner_by_id;
  SQLite::Statement add_activity;
};


inline Database& GetDb() {
  thread_local std::unique_ptr<Database> db;
  if (!db) {
    db = std::make_unique<Database>();
  }
  return *db;
}
