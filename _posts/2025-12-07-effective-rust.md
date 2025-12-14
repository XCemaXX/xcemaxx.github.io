---
title: "Effective Rust. David Drysdale"
categories:
  - Books
tags:
  - book
  - rust
classes: wide
---

[Effective Rust. David Drysdale](https://www.lurklurk.org/effective-rust)

Хорошая книга для закрепления базы
{: .notice--info .text-center}

Эту книгу проглотил, если сравнивать с предыдущей [про алгоритмы](/books/algo-kormen/): пара недель вместо пары месяцев.  

Две главы 3.1 и 3.2 посвящены lifetime и borrow-checker. Кажется, что каждая книга по Rust считает своим долгом объяснить эту тему заново: мол, сложно, уникально, никто не понимает. Я не согласен. У меня в обычном коде проблем уже не возникает. Сложности начинаются, когда появляется `unsafe`, указатели и желание передать такие структуры в асинхронный рантайм.   

В книге есть много отсылок к C++, и это оправдано. Я соглашусь, что объяснять Rust разработчику C++ проще. Возвращаясь к lifetime, в C++ только концепции `lvalue/rvalue` сложнее, чем вся тема lifetime.

## Повторение - мать учения

Хороший набор базовых вещей, которые полезно вспомнить:
- использовать типы для отражения семантики;
- паттерны NewType, Builder;
- различия между трейтами `AsRef`, `Borrow`, `ToOwned`;
- итераторы и их множество методов: концепция с одним `next`, с автоматической реализацией всех остальных интерфейсных методов, меня восхищает;
- `Drop` = RAII
- иерархия трейтов: `FnOnce` > `Fnmut` > `Fn`.
- преждеврменная оптимизация - зло;
- рассудительное использование макросов (декларативные и процедурные). Мне их почти не приходится писать, мало опыта с ними. Наверное, к лучшему;
- главное отличие `core::` от `std::` - отсутствие аллокатора `alloc::`

## Новое:
- Рефлексию в Rust лучше избегать, но если уж нужно: `std::any::{type_name, Any::type_id, TypeId}`. Я не фанат рефлексии, но знать полезно.
- Имена фич делят namespace с зависимыми крейтами: крейт может стать фичей, если объявить `optional = true`.
- Если интерфейс крейта использует типы из зависимых кретов, то стоит реэкспортировать либо крейт целиком, либо типы. По дизайну API лучше почитать отдельный [Rust API Guidelines](https://rust-lang.github.io/api-guidelines/about.html).

### Полезные крейты:
- Для ошибок есть два отличных крейта: [this_error](https://docs.rs/thiserror/latest/thiserror/) - если нужно потом их матчить, подходит для библиотек; [anyhow](https://docs.rs/anyhow/latest/anyhow/)/[eyre](https://docs.rs/eyre/latest/eyre/) - если планиурется просто их отображать, подходит для приложений.
- Крейт [ouroboros](https://docs.rs/ouroboros/latest/ouroboros/) для self-referencing структур.
- Документация: `cargo doc --no-deps --open`, `broken_intra_doc_links`, `#![warn(missing_docs)]`
- Управление деревом зависимостей: [cargo-udeps](https://docs.rs/crate/cargo-udeps/latest), [deny](https://docs.rs/crate/cargo-deny/latest), [cargo tree](https://doc.rust-lang.org/cargo/commands/cargo-tree.html), [cargo-machete](https://docs.rs/crate/cargo-machete/latest)
- FFI: [bingen](https://rust-lang.github.io/rust-bindgen/) и [cxx](https://cxx.rs).

### 1000 и 1 книга по Rust
В тексте много ссылок на другие Rust-книги, что навело меня на мысль сделать отдельный [пост со списком всех Rust book, которые я встречал](/notes/list-rust-books/). А себе отметил две для дальнейшего чтения:  
- [Software Engineering at Google](https://abseil.io/resources/swe-book)
- [Rust Atomics and Locks](https://marabos.nl/atomics/)  

## Бонус
В тему отлично ложится статья [Закрепи меня покрепче: Pin, самоссылки и почему всё падает](https://habr.com/ru/companies/beget/articles/967076/), где советуют крейт [pin_project](https://docs.rs/pin-project/latest/pin_project/).
