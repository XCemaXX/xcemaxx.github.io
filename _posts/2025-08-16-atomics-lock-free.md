---
title: "Атомики, Ordering, lock-free"
categories:
  - Notes
tags:
  - programming
classes: wide
---
Атомики — это искусство. Столкнулся с задачей на Rust: два атомика, три потока, а как расставить ордеринг так, чтобы и быстро, и корректно — хз. Руки тянулись везде поставить SeqCst.  

Решил систематизировать знания — так родился этот текст.  

## Атомики и CAS
Почти любая история про атомики начинается с CAS (compare-and-swap loop). Рекомендую два ресурса:  
- [Атомики, часть 1. Магистерский курс C++ (МФТИ, 2022-2023). Лекция 21.](https://youtu.be/JRUbzoVfkkw?si=9uCPqWLMR7xy3QqT&t=2815). Автор сразу начинает с кода, который по интуиции должен работать, но нет. Уводит разговор в неблокирующую синхронизацию.
- [Crust of Rust: Atomics and Memory Ordering](https://www.youtube.com/watch?v=rMGWeSjctlY&list=PLqbS7AVVErFiWDOAVrPt7aYmnuuOLYvOa&index=9). Автор объясняет тему атомиков на примере разработки своего `Mutex`. Разбирает типовые ошибки.  

Во время разговора о CAS loop всплывает следующая тема: Ordering.  
## Ordering
Ordering задаёт ограничения на переупорядочивания и видимость относительно операций с конкретной атомарной переменной. Хорошее объяснение можно найти в [Магистерский курс C++ (МФТИ, 2022-2023). Лекция 21. Атомики, часть 3](https://youtu.be/Y1q_Z2T2UcE?si=sC0SUM9XaS7Gf1-H).  

Краткая шпаргалка:
- **Relaxed**: нет никаких гарантий, кроме того, что операция будет выполнена атомарно. Все независимые операции от одной точки синхронизации до другой могут быть переупорядочены компилятором или процессором.
- **Release**: запрещает перемещать предшествующие операции в этом потоке после store; гарантирует, что все изменения в памяти до store станут видимы в другом потоке после соответствующего acquire-load этой же атомарной переменной.
- **Acquire**: запрещает перемещать последующие операции в этом потоке до load; гарантирует, что будут видны все изменения в памяти, выполненные до release-store этой же атомарной переменной в другом потоке.
- **AcqRel**: применяет обе семантики.
- **SeqCst**: AcqRel + глобальный единый порядок операций помеченных SeqCst для всех потоков, создавая happens-before между разными участками памяти. Happens-before отношения возникают только тогда, когда есть соответствующая пара Release/Acquire (или AcqRel) для одной и той же атомарной переменной, либо через SeqCst.

### Примеры
#### Синхронизация по флагу с  `Release`/`Acquire`
Пример `Ordering::Relaxed` с синхронизацией по одной переменной. Чинится заменой всех `Relaxed` на `Release`/`Acquire`:
```rust
static IS_DATA_CHANGED: AtomicI64 = AtomicI64::new(0);
static DATA: AtomicI64 = AtomicI64::new(0);
thread1: { 
  DATA.store(42, Ordering::Relaxed); // change to Release to fix
  IS_DATA_CHANGED.store(1, Ordering::Relaxed); // change to Release to fix
}
thread2: {
  if IS_DATA_CHANGED.load(Ordering::Relaxed) == 1 { // change to Acquire to fix
    let val = DATA.load(Ordering::Relaxed); // change to Acquire to fix
        if val != 42 {
            eprintln!("possible: saw FLAG=1 but DATA={}", val);
            process::abort();
        }
    }
}
```
Если оба потока используют `Ordering::Relaxed`, то компилятор или процессор могут изменить порядок операций внутри потока, и `DATA` может быть прочитан до того, как запись в него станет видимой.  

В итоге поток 2 может увидеть `IS_DATA_CHANGED == 1`, но при этом получить устаревшее значение `DATA`.  
#### Синхронизация двух переменных
Пример отсуствия `SeqCst` с двумя независимыми атомиками, который чинится заменой всех `Release`/`Acquire` на `SeqCst`:
```rust
static A: AtomicI64 = AtomicI64::new(0);
static B: AtomicI64 = AtomicI64::new(0);
thread1: loop {
    // In this thread, the store to A happens before the store to B
    A.fetch_add(1, Ordering::Release); // change to SeqCst to fix
    B.fetch_add(1, Ordering::Release); // change to SeqCst to fix
}
thread2: loop {
  // In this thread, the load of A is performed after the load of B
  // But globally, A is independent from B, and the ordering from thread1 is not enforced across threads
  let b = B.load(Ordering::Acquire); // change to SeqCst to fix
  let a = A.load(Ordering::Acquire); // change to SeqCst to fix
  // This makes it possible to observe:
  if a < b {
    eprintln!("possible: a={} < b={}", a, b);
    process::abort();
  }
  }
```
Даже при`Release`/`Acquire` возможна ситуация, когда поток 2 увидит `B` обновлённым, а `A` — ещё нет. Это связано с тем, что упорядочивание `Release`/`Acquire` действует только в рамках одной пары чтение/запись, но не накладывает глобального порядка на все атомики.

## Уровни неблокирующей синхронизации
Бывают три уровня синхронизации:
- **obstruction-free** - поток, запущенный в любой момент, завершит работу, если препятсвующие потоки не работают. Завершит работу за определенное колво шагов. Мьютекс не отвечает этой модели.
- **lock-free** - прогресс хотя бы одного потока. Это то место, где часто используются CAS loops
- **wait-free** - каждая операция выполняется за определённое количество шагов, не зависящее от других потоков.  

Написать правильный lock-free сложно, но wait-free - это next-level программирования. Очень интересен доклад по wait-free с [Introduction to Wait-free Algorithms in C++ Programming - Daniel Anderson - CppCon 2024](https://www.youtube.com/watch?v=kPh8pod0-gk).

# Заключение
Если ты начал задумываться, а не заменить ли мьютекс на атомик, то ответ скорее — нет. Статья [Spinlocks Considered Harmful](https://matklad.github.io/2020/01/02/spinlocks-considered-harmful.html) приводит пример, как один спинлок и неудачное расставление приоритетов потоков начинает безудержно крутить в холостых циклах весь процессор.  

Если сомневаешься насчёт Ordering, начинай с `SeqCst`, делай работающую версию, а потом снижай стоимость там, где это безопасно, и проверяй.  

В своей задаче из введения для проверки расставленных ордерингов подключил крейт `loom`. Он гоняет тестовый сценарий сотни раз, каждый раз меняя порядок доступа к атомикам. Это позволяет поймать гонки, которые в обычных тестах могут не воспроизвестись.  

Автор из [Crust of Rust: Atomics and Memory Ordering](https://www.youtube.com/watch?v=rMGWeSjctlY&list=PLqbS7AVVErFiWDOAVrPt7aYmnuuOLYvOa&index=9) даёт небольшую вводную про `loom` в конце видео.  
