---
title: "Запуск Rust приложения с ASan"
categories:
  - Notes
tags:
  - utilities
  - rust
classes: wide
---

Непросто писать многопоточный код. Точнее, непросто правильно пользоваться примитивами синхронизации.  

Недавно поймал баг в крейте [thingbuf](https://crates.io/crates/thingbuf) и завел [Issue](https://github.com/hawkw/thingbuf/issues/100). Это показывает, что open source не волшебство: разработчики такие же люди и тоже ошибаются.  

В разборе проблемы сильно помог санитайзер. Чтобы запустить его в Rust, нужно знать как это делать. Оставлю на память список команд.  

Замечу, под санитайзером тесты не запускаются. Они используют макросы, которые с ASan не работают. Поэтому я либо запускаю examples, либо создаю отдельный тестовый крейт для воспроизведения бага.
```sh
# Понадобится nightly Rust
rustup toolchain install nightly
rustc +nightly --version
rustup override set nightly
rustup override unset # отключить для дальнейшей разработки

# ASan
sudo apt install clang llvm libclang-rt-dev

RUSTFLAGS="-Zsanitizer=address -Clink-arg=-fno-omit-frame-pointer \
  -Cforce-frame-pointers=yes -Cdebuginfo=2" cargo run --target x86_64-unknown-linux-gnu

# Найти бинарник example
RUSTFLAGS="-Zsanitizer=address -Clink-arg=-fno-omit-frame-pointer \
  -Cforce-frame-pointers=yes -Cdebuginfo=2" cargo test --no-run --message-format=json \
  --target x86_64-unknown-linux-gnu > example_bin.json

# ASAN_OPTIONS — без пробелов
ASAN_OPTIONS="detect_leaks=1:fast_unwind_on_malloc=0:alloc_dealloc_mismatch=0 \
  :detect_stack_use_after_return=1:halt_on_error=1:abort_on_error=1:strict_init_order=1 \
  :symbolize=1:verbosity=2" "./target/x86_64-unknown-linux-gnu/debug/deps/test_proj-256ea6bc7957389e"

# Запуск под gdb аналогичен
gdb --args env ASAN_OPTIONS="..." "./target/x86_64-unknown-linux-gnu/debug/deps/test_proj-256ea6bc7957389e"
```

Прикупил себе книжку C++ Concurrency in action [2019] Энтони Уильямс. Изучу вместе с другой [Rust Atomics and Locks [2023] Мара Бос](https://marabos.nl/atomics/).  

Стану мастером многопотока 😎  

Напишу свою SCSP очередь с переиспользованием слотов 🚲