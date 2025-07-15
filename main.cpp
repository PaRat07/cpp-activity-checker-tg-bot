#include <exception>
#include <utility>
#include <memory>
#include <new>
#include <tgbm/bot.hpp>

#include <tgbm/utils/formatters.hpp>
#include <tgbm/utils/scope_exit.hpp>

#include "database.h"

static tgbm::api::reply_markup_t GetDataCollectorMessage() {
  using namespace tgbm::api;
  using button = InlineKeyboardButton;
  reply_markup_t markup;

  auto& m = markup.emplace<InlineKeyboardMarkup>();
  m.inline_keyboard = {
      {{.text = "Отметить свою активность", .data = button::callback_data{.value = "mark_active"}}},
  };
  return markup;
}

static dd::task<void> answer_query(tgbm::bot& bot, tgbm::api::CallbackQuery q) {
  using namespace tgbm::api;

  // if (q.data)

  bool success = co_await bot.api.answerCallbackQuery({
    .callback_query_id = q.id,
    .text = q.data
  });
  if (!success) {
    fmt::println(stderr, "bot cannot handle callback query =(");
    co_return;
  }

  int_or_str id;
  if (q.message)
    id = q.message->chat->id;

  Message sended = co_await bot.api.sendMessage({
      .chat_id = id,
      .text = fmt::format("your answer is {}", q.data.value_or("<nothing>")),
  });
  fmt::println("user receives: {}", sended);
}

dd::task<void> start_main_task(tgbm::bot& bot) {
  on_scope_exit {
    // stop bot on failure
    bot.stop();
  };
  fmt::println("launching echobot, info: {}", co_await bot.api.getMe());

  co_foreach(tgbm::api::Update && u, bot.updates()) {
    tgbm::api::CallbackQuery* cq = u.get_callback_query();
    if (cq)
      answer_query(bot, std::move(*cq)).start_and_detach();
  }
}

int main() {
  const char* token = std::getenv("BOT_TOKEN");
  if (!token) {
    fmt::println("launching telegram bot requires bot token from @BotFather");
    return -1;
  }
  Database db;

  tgbm::bot bot{token /*"api.telegram.org", "some_ssl_certificate"*/};

  bot.commands.add("get_keyboard", [&bot, &db](tgbm::api::Message&& m) {
    std::invoke([&bot, m, &db] -> dd::task<void> {
      db.AddActivityCheck();
      co_await bot.api.sendMessage({
        .chat_id = m.chat->id,
        .text = fmt::format("ID этого опроса: {}")
      });

    }).start_and_detach();
    dd::task sendmsg = bot.api.sendMessage({
        .chat_id = m.chat->id,
        .text = "Отметьте свою активность",
        .reply_markup = GetDataCollectorMessage(),
    });
    sendmsg.start_and_detach();
  });

  start_main_task(bot).start_and_detach();
  bot.run();

  return 0;
}
