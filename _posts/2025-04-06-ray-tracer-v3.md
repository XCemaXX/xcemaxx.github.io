---
title: "Rust Ray Tracing: рефакторинг, архитектура и параллелизм"
categories:
  - Notes
tags:
  - programming
  - rust
classes: wide
---
Продолжил работу над **Ray Tracer** на Rust. Результаты первой части [описывал ранее](/article/study-rust-2025/).

По итогам [второй части туториала](https://raytracing.github.io/books/RayTracingTheNextWeek.html) сделал [рендер картинки](https://github.com/XCemaXX/rust_study_path/tree/ray_tracer_v2/06_tiny_stuff/05_ray_tracing) за час на 16 ядрах:
<figure style="display: flex; justify-content: center;">
  <img src="https://github.com/XCemaXX/rust_study_path/blob//ray_tracer_v2/06_tiny_stuff/05_ray_tracing/out_v2_56m.png?raw=true" style="width: 100%; max-width: 500px;" alt="Ray tracer v2">
</figure>
Благодаря [третьей части туториала](https://raytracing.github.io/books/RayTracingTheRestOfYourLife.html#onedimensionalmontecarlointegration/importancesampling) качество рендера улучшилось. Добавил оптимизацию параллелизма и рендер картинки выше сократилось до 25 минут. Изменения качества особенно заметно в Корнеллской коробке на малом количестве лучей (все параметры рендера идентичны):
<figure style="display: flex; justify-content: center;">
  <img src="{{ '/assets/images/2025-04-06-cornell-box-comp.png' | relative_url }}" style="width: 100%; max-width: 700px;" alt="Ray tracer v2 vs v3">
</figure>
[Итог третьей части](https://github.com/XCemaXX/rust_study_path/tree/ray_tracer_v3/06_tiny_stuff/05_ray_tracing) с стеклянными и зеркальными поверхностями:
<figure style="display: flex; justify-content: center;">
  <img src="https://github.com/XCemaXX/rust_study_path/blob/ray_tracer_v3/06_tiny_stuff/05_ray_tracing/out_v3_13m.png?raw=true" style="width: 100%; max-width: 500px;" alt="Ray tracer v3">
</figure>

# Запомнившиеся моменты
- **Рефакторинг ссылок**. Избавился от лишнего `Box` в `Arc` для текстур и материалов, чтобы удобно делиться ими между потоками.
- **Трейты Send/Sync**. Перенёс `Send + Sync` в сами трейты, а не раскидывал их через generic-параметры.
- **Унифицированный конструктор через трейты**. Ввел трейт `IntoSharedMaterial`, позволяющий писать единый конструктор вместо дублирования `from_shared_texture`, `from_texture`:

```  
pub trait IntoSharedMaterial {
    fn into_arc(self) -> Arc<dyn Material>;
}

impl IntoSharedMaterial for Arc<dyn Material> {
    fn into_arc(self) -> Arc<dyn Material> {
        self
    }
}

impl<T: Material + 'static> IntoSharedMaterial for T {
    fn into_arc(self) -> Arc<dyn Material> {
        Arc::new(self)
    }
}
```

- **Трансформации объектов**. Создал трейт `Transformable` для поворота и перемещения объектов через преобразование типов. Аналогично сделаны итераторы, `map`, `filter` и т.д.  

```
pub struct RotateY<T: Hit> { // Все объекты характеризуются трейтом Hit в проекте
    object: T,
}
impl<T: Hit> Hit for RotateY<T> { ... }

pub trait Transformable: Hit + Sized {
    fn rotate_y(self, angle: f32) -> RotateY<Self> {
        RotateY::new(self, angle)
    }
    fn translate(self, offset: Coords) -> Translate<Self> {
        Translate::new(self, offset)
    }
}

impl<T: Hit> Transformable for T {}

// В вызывающем коде: object.rotate_y(45.).translate(2.)
```  

- **Option оператор `?`**. Упростил обработку опциональных значений.
- **Сборка объектов через `FromIterator`**. Реализовал конструктор с помощью трейта `FromIterator` и `fold`.
- **Функциональный стиль**. Использовал итераторы, `map`, `collect` и `flat_map` для лаконичного и декларативного кода.
- **Параллелизм и каналы**. Организовал worker pool с использованием каналов для снижения времени рендера.
- **Многопоточные генераторы чисел**. Распихал по проекту `thread_local!` для генераторов случайных чисел.
- **Ссылки без лишних аллокаций**. Применял `Arc` и `&'a dyn` там, где это возможно. Без трейта `clone` в проекте не обошлось, к сожалению.

## Не туториалом единым
Рекомендую глянуть статью [Rust в режиме «жесть»](https://habr.com/ru/articles/893910/), где Алексей описывает код рейтресера с минимизацией взаимодействия с `std`.
