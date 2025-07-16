#include <exception>
#include <utility>
#include <memory>
#include <new>
#include <tgbm/bot.hpp>

#include <tgbm/utils/formatters.hpp>
#include <tgbm/utils/scope_exit.hpp>

#include "database.h"

#include <charconv>

constexpr std::string_view kCallbackMarkActivePrefix = "mark_active ";

static tgbm::api::InlineKeyboardMarkup GetDataCollectorMessage(int64_t id) {
  using namespace tgbm::api;
  using button = InlineKeyboardButton;
  InlineKeyboardMarkup markup;

  markup.inline_keyboard = {
      {{.text = "Отметить", .data = button::callback_data{.value = fmt::format("mark_active {}", id)}}},
  };
  return markup;
}

std::optional<int64_t> ParseMarkActiveQuery(tgbm::api::optional<std::string> &query) {
  if (query.has_value()) {
    std::string_view str = query.value();
    if (str.starts_with(kCallbackMarkActivePrefix)) {
      int64_t ans;
      if (std::from_chars(str.data() + kCallbackMarkActivePrefix.size() , str.data() + str.size(), ans).ec != std::errc()) {
        return std::nullopt;
      }
      return ans;
    } else {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }
}

static dd::task<void> answer_query(tgbm::bot& bot, tgbm::api::CallbackQuery q) {
  using namespace tgbm::api;

  int64_t act_check_id;
  if (auto id_opt = ParseMarkActiveQuery(q.data); id_opt.has_value()) {
    act_check_id = id_opt.value();
  } else {
    co_await bot.api.answerCallbackQuery({
      .callback_query_id = q.id,
        .text = fmt::format("Некорректный формат текста обратного вызова кнопки: \"{}\"", q.data.value_or("<empty>"))
    });
    co_return;
  }

  std::string_view message = GetDb().AddActivity(act_check_id, q.from->id, fmt::format("{} {}", q.from->first_name, q.from->last_name.value_or("")), std::chrono::system_clock::now());

  bool success = co_await bot.api.answerCallbackQuery({
    .callback_query_id = q.id,
    .text = std::string(message)
  });
  if (!success) {
    TGBM_LOG_ERROR("bot cannot handle callback query =(");
    co_return;
  }
}

dd::task<void> AnswerInlineQuery(tgbm::bot& bot, tgbm::api::InlineQuery q) {
  using namespace tgbm::api;


  int64_t act_id = GetDb().AddActivityCheck(q.from->id);
  InlineQueryResultArticle res {
    .id = "RESULT ID",
      .title = fmt::format("Отметьте свою активность (ID проверки: )", act_id),
      .description = "Description",
    .input_message_content = InputTextMessageContent{ .message_text = "Message Text" }
      // .reply_markup = GetDataCollectorMessage(act_id)
  };

  co_await bot.api.answerInlineQuery({
    .inline_query_id = q.id,
    .results = { InlineQueryResult{res} },
    .is_personal = true,
  });
}

dd::task<void> start_main_task(tgbm::bot& bot) {
  on_scope_exit {
    // stop bot on failure
    bot.stop();
  };
  fmt::println("launching echobot, info: {}", co_await bot.api.getMe());

  co_foreach(tgbm::api::Update && u, bot.updates()) {

    if (auto *cq = u.get_callback_query(); cq) {
      answer_query(bot, std::move(*cq)).start_and_detach();
    } else if (auto *iq = u.get_inline_query(); iq) {
      AnswerInlineQuery(bot, std::move(*iq)).start_and_detach();
    }
  }
}

int main() {
  const char* token = std::getenv("BOT_TOKEN");
  if (!token) {
    fmt::println("launching telegram bot requires bot token from @BotFather");
    return -1;
  }

  tgbm::bot bot{token /*"api.telegram.org", "some_ssl_certificate"*/};

  bot.commands.add("get_keyboard", [bot_ptr = &bot](tgbm::api::Message&& m) {
    std::invoke([] (tgbm::bot &bot, tgbm::api::Integer chat_id) -> dd::task<void> {
      int64_t act_id = GetDb().AddActivityCheck(chat_id);
      co_await bot.api.sendMessage({
        .chat_id = chat_id,
        .text = fmt::format("ID этой проверки активности: {}", act_id)
      });
      co_await bot.api.sendMessage({
        .chat_id = chat_id,
        .reply_markup = GetDataCollectorMessage(act_id),
        .text = "Отметьте свою активность"
      });
    }, *bot_ptr, m.chat->id).start_and_detach();
  });

  start_main_task(bot).start_and_detach();
  bot.run();

  return 0;
}
