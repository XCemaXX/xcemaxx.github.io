---
title: "Связные списки, unsafe, Stacked Borrows"
categories:
  - Article
tags:
  - programming
  - rust
  - study
classes: wide
---
Связные списки преследуют меня со времён университета — на экзамене по C я впервые писал их в ответе на бумаге, а на собеседованиях просили реализовать не меньше пяти раз.  

## Списки на Rust
В C односвязный список — это узел с данными и указателем на следующий элемент, а сама структура хранит ссылку на голову.  

Когда я начал изучать Rust, то у меня порвало шаблон. Я увидел [реализацию связного списка](https://google.github.io/comprehensive-rust/smart-pointers/box.html) в функциональном стиле в виде одного `enum` без указателей:  

```rust
enum List<T> {
    Element(T, Box<List<T>>),
    Nil
}
```
Без unsafe, указателей, без дополнительных структур — и это, действительно, [работает](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/01_basic/list_one_struct.rs).  

### Связные списки на практике
В решении задачи с [linked list](https://exercism.org/tracks/rust/exercises/simple-linked-list), я написал реализацию списка, как по учебнику C, только без указателей, чтобы обойтись без unsafe:  

```rust
struct Node<T> {
    data: T,
    next: Option<Box<Node<T>>>,
}
pub struct SimpleLinkedList<T> {
    len: usize,
    head: Option<Box<Node<T>>>,
}
```

С ним проблем нет, пока нужен только односвязный список.

На [тренировках яндекса по алгоритмам](/notes/yandex-contest/) я реализовывал граф, где хранил узлы в `Vec<Node>` и использовал индексы вместо сырых указателей. Так с помощью костыля я обошёл ограничения владения, которые возникали из-за множественых связей между узлами.  

## Too many linked lists
Из-за проблемы с множественными указателями захотелось углубиться в тему. Я нашел отличный туториал [Learn Rust With Entirely Too Many Linked Lists](https://rust-unofficial.github.io/too-many-lists/).

### Стэки на односвязных списках
[Первая нормальная реализация](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/02_stacks/second.rs) односвязного списка в туториале классическая, разве что добавлен удобный алиас:
```
type Link<T> = Option<Box<Node<T>>>;
```
Минус этой реализации - шарить части списка не получится. Эту проблему мы будем исправлять на следующем шаге.  

Для [реализации неизменяемого односвязного списка](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/02_stacks/third.rs) с общими (разделяемыми) частями понадобится `Rc`. Этот подход можно уже считать решением задачи с графом, которую я обходил через хранение узлов в векторе. Здесь все честно, ноды лежат в памяти без самописного аллокатора:
```rust
//list1 -> A -+
//            |
//            v
//list2 ----> B -> C -> D
//            ^
//            |
//list3 -> X -+

type Link<T> = Option<Rc<Node<T>>>;

struct Node<T> {
    elem: T,
    next: Link<T>,
}

pub struct List<T> {
    head: Link<T>,
}
```

### Очередь на двухсвязном списке
Первая попытка [реализации двухсвязного списка](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/03_queues/fourth_safe.rs) оказывается полууспешной - список рабоает, но без возможности сделать мутирующий итератор.
```rust
type Link<T> = Option<Rc<RefCell<Node<T>>>>;

struct Node<T> {
    elem: T,
    next: Link<T>,
    prev: Link<T>,
}

struct List<T> {
    head: Link<T>,
    tail: Link<T>,
}
```
В такой реализации удобный API для итерации, возвращающего ссылки, конфликтует с `Rc<RefCell<...>>` – нарушаются правила владения и заимствования в Rust. `RefCell` облегчает управление изменениями внутри списка, но затрудняет извлечение данных через итераторы.  Решить проблему можно, если например, в итераторе возвращать детали внутренней реализации - не просто ссылку, а `Rc<RefCell<...>>`. Но это не наш путь, поэтому погружаемся глубже.

### Unsafe и Stacked Borrows
[Решение проблемы двусвязного списка](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/03_queues/fifth_unsafe.rs) с API, не раскрывающим детали реализации, проходит через unsafe Rust:
```rust
type Link<T> = Option<Box<Node<T>>>;

struct Node<T> {
    elem: T,
    next: Link<T>,
}

struct List<T> {
    head: Link<T>,
    tail: *mut Node<T>,
}
```
Эта ключевая глава туториала, где автор приходит к теме ["Stacked Borrows"](https://rust-unofficial.github.io/too-many-lists/fifth-stacked-borrows.html) и [детально её объясняет](https://rust-unofficial.github.io/too-many-lists/fifth-testing-stacked-borrows.html).  

"Stacked Borrows" - механизм, с помощью которого Rust отслеживает, какие указатели имеют право доступа к данным в конкретный момент времени.
Указатель помещается на вершину «стека заимствований» при каждом новом заимствовании, например, когда ссылка преобразуется в сырой указатель `*mut` или `*const`. В каждый момент времени доступ к данным разрешён только через указатель, находящийся на вершине стека, что предотвращает одновременное мутирующее обращение через несколько указателей:

```rust
let mut data = 10;
let ref1 = &mut data;
let ref2 = &mut *ref1;

*ref2 += 2;
*ref1 += 1; // можно, так как ref2 уже нигде не используется
// Будет ошибка, если изменить порядок
// *ref1 += 1; // к ref1 нельзя обратиться, так как дальше идет обращение к ref2
// *ref2 += 2;
```

Если на стек заимствований попадает неизменяемый указатель `*const`, который разрешает только чтение, то все последующие заимствования тоже будут в режиме чтения, даже если они будут указателями `*mut`. Это гарантирует, что после появления неизменяемого заимствования дальнейшие обращения к данным будут только для чтения, что защищает их от изменения.  

Понимание этого механизма придет быстрее, если предварительно прочесть [The Rustonomicon](https://doc.rust-lang.org/nomicon). В ходе данного туториала автор отсылается к этой книге.  

### Пишем библиотечный код
В последней части туториала автор переходит к production-ready linked list. Уходит от сырых указателей к `Option<NonNull<>>` и `PhantomData`, потому что `*mut T` не ковариантен отосительно `T`, а `NonNull` уже ковариантен.

Свойство ковариантности нужно для того, чтобы коллекция по `T` была коварианта `T`, что позволяет подставить `List<&'a T>` вместо `List<&'b T>`, где `a` - дольше живет.  

Учтя все это, получаем [такую структуру](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/05_queue_production/linked_list/mod.rs):
```rust
struct Node<T> {
    elem: T,
    front: Link<T>,
    back: Link<T>,
}

type Link<T> = Option<NonNull<Node<T>>>;

struct List<T> {
    front: Link<T>,
    back: Link<T>,
    len: usize,
    _boo: PhantomData<T>,
}

```

Следующие несколько глав идет повторение пройденного и тележка болерплейта для реализации интерфейса `front/back` и трейтов. Но есть и новое, автор вводит `CursorMut`, который позволяет делать `splice` и `split`:

```rust
pub struct CursorMut<'a, T> {
    cur: Link<T>,
    list: &'a mut LinkedList<T>,
    index: Option<usize>,
}
```
Как дополнительное упражнение, я написал [реализацию методов](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/05_queue_production/linked_list/cursor.rs) `remove`, `insert` для `Cursor`.

### Связный список на стеке - не повторять
В заключении автор забавы ради приводит [связный список на стеке](https://github.com/XCemaXX/rust_study_path/blob/too_many_linked_lists/11_linked_lists/06_silly/reqursive_on_stack.rs), который каждый новый элемент аллоцирует через `callback`:

```rust
pub struct List<'a, T> {
    pub data: T,
    pub prev: Option<&'a List<'a, T>>,
}

pub fn push<U>(
    prev: Option<&'a List<'a, T>>,
    data: T,
    callback: impl FnOnce(&List<'a, T>) -> U,
) -> U {
    let list = Self { data, prev };
    callback(&list)
}

// user code
List::push(None, Cell::new(3), |list| {
            List::push(Some(list), Cell::new(5), |list| {
                List::push(Some(list), Cell::new(13), |list| {
                    ...
}}}
```

## Заключение
«Too Many Linked Lists» — отличный путь от простых списков до production-ready библиотеки, где придется познакомится с указателями и ссылками, Stacked Borrow, unsafe, PhantomData, MIRI.  

Прочитав этот материал можно изучить добрую часть концепций языка Rust. Но лучше использовать его для повторения и закрепления, когда все уже изучено по другим материалам. Поэтому рекомендую [пререквизиты для туториала](/article/study-rust-2025/).