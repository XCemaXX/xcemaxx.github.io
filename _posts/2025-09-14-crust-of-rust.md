---
title: "Crust of rust - серия стримов для углубления знаний по Rust"
categories:
  - Notes
tags:
  - programming
  - rust
  - study
classes: wide
---

После изучения базы по Rust (подробности в [гайде](/article/study-rust-2025/)) я стал искать, как углубить знания. Попалась [неудачная серия материалов](notes/study-rust-2025-p2/), но потом я нашёл то, что нужно — YouTube-канал Jon Gjengset с отличной серией видео [Crust of Rust](https://www.youtube.com/playlist?list=PLqbS7AVVErFiWDOAVrPt7aYmnuuOLYvOa).  

С первой лекции про lifetime annotations я понял: это именно тот контент, которого не хватало. Ниже — обзор тем Crust of Rust и заметки того, что зацепило.

## Lifetime annotations

Lifetimes — первое, что бросается в глаза в Rust и часто даётся тяжело. В других языках этого почти нет, поэтому приходится перестраивать привычное мышление.  

В лекции тема раскрыта хорошо. Самое классное, что автор затрагивает множество полезных подтем. Например, как писать итератор, чем отличаются `String` и `&str`, как использовать `ref mut`, `Option::take`, возврат `Option?` из функции.

## Subtyping и variance

Повторил для себя subtyping и variance. Автор объясняет чётко, но без базовых знаний видео будет тяжело восприниматься. Для закрепления материала он советует [Lifetime variance in Rust](https://lifetime-variance.sunshowers.io/index.html).

Из интересного:

* для владеющего типа через указатель стоит добавлять `PhantomData` и использовать `NonNull` вместо сырого указателя — такой тип будет инвариантен по `T`;
* для невладеющего типа нужно задавать variance вручную через `PhantomData` иногда с подвывертом, чтобы указать нужный variance.

Пример:

```rust
struct OwningType<T> {
  p: NonNull<T>, // *mut T,
  _pd: PhantomData<T>
}
struct NotOwningType<T> {
  p: *mut T,
  _covariant: PhantomData<fn()->T>,
  //_contravariant: PhantomData<fn(_:T)->()>,
  //_invariant: PhantomData<fn(_:T)->T>,
}
```

## Drop check

При реализации `Drop` компилятор предполагает доступ `&mut` к полям. Поэтому в таком типе нельзя сделать partial move из структуры. В Nightly Rust есть обход через `#[may_dangle]`.

## Dynamic dispatch

Не все трейты превращаются в trait object (`dyn SomeTrait`). Сигнатура методов трейта кодируется в vtable для `dyn`, и некоторые формы методов в таблицу поместить нельзя.

**Волшебное** ограничение  `where Self: Sized` может решить несколько проблем:

* позволяет делать `dyn SomeTrait`, блокируя методы, несовместимые с object safety:

  * статические методы и ассоциированные функции (без отсылок к `self`);
  * методы, принимающие или возвращающие `self` по значению;
  * обобщённые (generic) методы. В этом случае есть обход через динамическую диспетчеризацию;
* полностью запрещает object safety, если повесить условие на сам трейт. Редкий кейс, но иногда применяется.

```rust
trait SomeTrait {
  type SomeInnerTraitType;
  fn usual_method(&self);
  fn static_method() where Self: Sized;
  fn by_value(self) where Self: Sized;
  fn ret_by_value(&self) -> Self where Self: Sized;
  fn template_method<T: AnotherTrait>(&self, x:&T) where Self: Sized;
  fn template_method_dyn(&self, x:&dyn AnotherTrait);
}
fn foo(t: &dyn SomeTrait<SomeInnerTraitType = ConcreteType>);
```

## ?Sized

По умолчанию generic-параметры (`T`) считаются `Sized`. Чтобы принимать unsized-типы, нужно явно писать `?Sized`.  

В трейтах наоборот: они определяются как `Self: ?Sized`, чтобы компилятор мог автоматически выводить `dyn SomeTrait`.
В итоге:

* в generic-функциях надо явно указывать `Sized` или `?Sized`;
* в трейтах это не требуется.

```rust
trait SomeTrait { }
fn foo<T: SomeTrait>(t: &T) { } // fail to call foo(&"hello")

// компилятор разворачивает так:
trait SomeTrait where Self: ?Sized { }
fn foo<T: SomeTrait + Sized>(t: &T) { }
impl SomeTrait for dyn SomeTrait {}

// для unsized
fn foo<T: SomeTrait + ?Sized>(t: &T) { } // ok to call foo(&"hello")
```

## Async/await

Вводная лекция по асинхронному программированию. У автора есть более глубокие разборы:

* [The What and How of Futures and async/await in Rust](https://www.youtube.com/watch?v=9_3krAQtD2k&list=PLqbS7AVVErFgMPqz5irpWbBInR6hxDbYi&index=2)
* [Decrusting the tokio crate](https://www.youtube.com/watch?v=o2ob8zkeq2s&list=PLqbS7AVVErFirH9armw8yXlE6dacF-A6z&index=5)
* [Building an asynchronous ZooKeeper client in Rust](https://www.youtube.com/watch?v=mMuk8Rn9HBg)

Но все-таки что то да вынес для себя:

* при `select/join` на нескольких future они poll’ятся в текущем task; чтобы реально вынести на другие потоки, нужен `spawn`;
* `#[async_trait]` — удобный сахар, но он прячет аллокацию в куче (`Box<dyn Future>`); сейчас уже есть [поддержка async trait](https://blog.rust-lang.org/2023/12/21/async-fn-rpit-in-traits/), и если не нужен `dyn SomeAsyncTrait`, можно обойтись без этого макроса;
* обычные мьютексы можно использовать в async-функциях, если внутри критической секции нет `.await`; иначе нужен async-мьютекс.

```rust
#[async_trait]
trait SomeTrait {
  async fn foo(&mut self) -> SomeType;
  // desugars to:
  // fn foo(&mut self) -> Pin<Box<dyn Future<Output = SomeType>>>;
}
trait NotDynTrait {
  async fn foo(&mut self) -> SomeType;
  // desugars to:
  // fn foo(&mut self) -> impl Future<Output = SomeType>;
}
```

## Build scripts / FFI

В других изученных мной материалах про это почти не было, а на работе буквально первые задачи были про использование сторонних библиотек. В Rust-мире знание про биндинги критично: любой серьёзный проект будет использовать внешние библиотеки, а не только крейты на Rust.

Build scripts (`build.rs`) — это возможность cargo запускать код до сборки. По сути, аналог CMake-логики в C++. Без них FFI не обойтись: хотя бы для линковки shared-library.

На практике:

1. Пишем `build.rs` с `println!("cargo:rustc-link-lib=dylib=mylib");`.
2. Для биндингов используем `bindgen`.
3. Можно пробовать `cbindgen`, `cxx` или `autocxx` для упрощения жизни.

Доп. видео из лекции [Unsafe & FFI in Rust / Ryan Levick](https://www.youtube.com/watch?v=LFFbTeU25pE) можно пропустить — там примитив. Гораздо полезнее будет посмотреть доклад Jack O’Connor:

* [From Rust to C and Back Again — Seattle Rust User Group, April 2025](https://www.youtube.com/watch?v=B4yNqR0WgYQ) (пример с `small_vec`)
* [From Rust to C and Back Again: an introduction to "foreign functions"](https://www.youtube.com/watch?v=LLAUzghhNHg) (пример с `linked_list`).

## Заключение

Из дополнительных плейлистов Jon Gjengset я хочу посмотреть:

* [Advanced topics in Rust](https://www.youtube.com/playlist?list=PLqbS7AVVErFgMPqz5irpWbBInR6hxDbYi)
* [Decrusted](https://www.youtube.com/playlist?list=PLqbS7AVVErFirH9armw8yXlE6dacF-A6z) — разбор крейтов.  Хочу глянуть, как минимум, `tokio` и `serde`.

Читал мнение, что стримы с кодингом не помогают учиться программированию. Возможно это так, но у меня есть практика на работе, а дома я в спокойном режиме слушаю и подтягиваю теорию.  

Crust of Rust — это не просто "стрим с кодом", скорее похоже на практику в универе, где тщательно разбирается один аспект языка.