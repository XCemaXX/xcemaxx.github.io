---
title: "Паттерны проектирования от Игльбергера Клауса и Шона Парента"
categories:
  - Notes
tags:
  - programming
#layout: splash
classes: wide
---
Паттерны проектирования - это база для мидла, но правильно их готовить - дело сложное.
В выступлении [Design Patterns: The most common misconceptions - Klaus Iglberger - Meeting C++ 2023](https://www.youtube.com/watch?v=w-fkFyrbWbs) объясняется разница концепций "паттерны проектирования" и "детали реализации". Для этого автор вводит понятие "границы" между частями кода, которая определяет, где заканчиваются детали и начинается архитектурная идея.

Например, автор показывает, как путают паттерны `Builder` или `Factory` с их реализациями:
- `GoF Builder` фокусируется на поэтапном создании сложных объектов, тогда как `Bloch Builder (fluent interface API)` скорее помогает улучшить читаемость кода.
- `Factory Method` используется в библиотеках, где одна функция нуждается в другой, чтобы создать объект, а `Factory Function` просто служит для упрощения создания объектов. Классический пример `Factory Method` - `std::generate`.  

Автор разбирает идеому `Pimpl` в купе с `Bridge` и `Strategy`. На первый взгляд, оба паттерна используют указатель на объект, но их отличия лежат в подходе к кастомизации:  
- `Bridge` — внутренняя точка кастомизации, так как класс сам создает объект.
- `Strategy` — внешняя точка кастомизации, так как класс получает готовый объект извне.  

В стандартной библиотеке C++ `Strategy` можно встретить частенько:
- передача deleter в `std::unique_ptr`
- передача аллокатора в `std::vector`
- разные функции в `std::set`
- написать операцию в `std::accumulate`

В продолжении доклада [C++ Design Patterns - The Most Common Misconceptions (2 of N) - Klaus Iglberger - CppCon 2024](https://www.youtube.com/watch?v=pmdwAf6hCWg), Клаус углубляется в концепцию `CRTP`, которая может использоваться для создания статических интерфейсов или реализации миксинов.
  
Наконец, Клаус сравнивает `std::variant` и виртуальные функции как подходы для хранения объектов разных типов с общей функциональностью. Несмотря на привлекательность `std::variant`, он не заменяет виртуальные функции, а лишь предлагает альтернативу со своими особенностями.  

# Пример создания вложенных структур
В докладе 2024 года Игльбергер Клаус ссылается на выступление "Inheritance Is The Base Class of Evil" Sean Parent. Шон Парент создает документ, который поддерживает историю операций. Такого элегантного способа сделать **redo** и коллекцию разноплановых объектов я не встречал.  

Первую часть этого примера [рассмотрел и доработал Константин Владимиров на русском](https://youtu.be/_Jn7MAZYL2M?si=VjAR2RRgUdp1usc8&t=4445). [Доработанный код представлен на godbolt](https://godbolt.org/z/xEPrf7azx). Оригинальный код от Шона Парента я не смог скомпилировать.  

К сожалению, в компилируемом варианте основная фича с добавлением новых типов объектов работает не идеально. Свои объекты нужно объявлять до кода библиотеки. Если разделить код Констанина Владимирова на две части: user, library - как это сделал Шон Парент, то рабочий вариант будет выглядить так:
```
class myclass_t {};

void draw(const myclass_t &, std::ostream &out, size_t position) {
  out << std::string(position, ' ') << "myclass_t" << std::endl;
}
#include "document.hpp"
```
Если изменить порядок заголовочного файла и определения класса, то компилятор не может вывести шаблон:
```
template<typename T>
void draw(const T& object, std::ostream& out, size_t position) {
    out << std::string(position, ' ') << object << std::endl;
}

void model::draw_(std::ostream &out, size_t position) const override {
  ::draw(data_, out, position);
  // ERROR: in instantiation of function template specialization 'draw<myclass_t>' requested here
}
```
С помощью Argument-Dependent Lookup удалось решить и эту проблему. [См. итоговый код на godbolt](https://godbolt.org/z/s5K8Wzn8a) или ниже.  

## Рекомендации по литературе
После просмотра этих докладов у меня возникло желание углубиться в тему и прочесть книгу "Проектирование программ на C++" Клауса Игльбергера. 

Если вы только начинаете знакомство с паттернами, могу порекомендовать свою первую книгу на эту тему — "Head First. Паттерны проектирования" Эрика Фримена и Элизабет Робсон. Книга о патернах и только паттернах - стратегия, фабрика, посетитель и многие другие. Примеры написаны на Java, но изложение доступное, так что даже если вы не знакомы с языком, понять суть не составит труда.

Для практического изучения и рефакторинга паттернов рекомендую сайт [refactoring.guru](https://refactoring.guru/), где информация представлена структурировано и с реальными примерами.  

Также в моем списке для чтения — книга [Game Programming Patterns. Robert Nystrom](https://gameprogrammingpatterns.com). Помимо чтения, я хочу реализовать проект: создать мини-игры, в которых каждый паттерн будет не только использоваться в коде, но и проявляться в самом геймплее.  

## Доработанный пример
```
// ---------  lib_doc.hpp
#include <ostream>
#include <string>
#include <vector>
#include <memory>

// Объявление
template <typename T>
void draw(const T& object, std::ostream& out, size_t position);

// Добавил wrapper, который вызываю в model_t::draw
template<typename T>
void draw_wrapper(const T& object, std::ostream& out, size_t position) {
    using ::draw;
    draw(object, out, position);
}

class object_t;
using document_t = std::vector<object_t>;

void draw(const document_t &x, std::ostream &out, size_t position);

class object_t {
public:
    template <typename T>
    object_t(T x) : pimpl_(new model_t<T>(std::move(x))) {}

    friend void draw(const object_t& object, std::ostream& out, size_t position) {
        object.pimpl_->draw(out, position + 2);
    }

private:
    struct concept_t {
        virtual void draw(std::ostream& out, size_t position) const = 0;
        virtual std::unique_ptr<concept_t> copy_() const = 0;
        virtual ~concept_t() = default;
    };

    template <typename T>
    struct model_t final : concept_t {
        model_t(T x) : data_(std::move(x)) {}
        void draw(std::ostream& out, size_t position) const override {
            // Вызов wrapper
            ::draw_wrapper(data_, out, position); 
        }
        std::unique_ptr<concept_t> copy_() const override {
            return std::make_unique<model_t>(*this);
        }
        T data_;
    };

    std::unique_ptr<concept_t> pimpl_;
  // copy ctor, move ctor and assignment
public:
    object_t(const object_t &x) : pimpl_(x.pimpl_->copy_()) {}
    object_t(object_t &&x) noexcept = default;

    object_t &operator=(object_t x) noexcept {
      pimpl_ = std::move(x.pimpl_);
      return *this;
    }
};

void draw(const document_t& document, std::ostream& out, size_t position) {
    out << std::string(position, ' ') << "<document>" << std::endl;
    for (const auto& element : document) {
        draw(element, out, position + 2);
    }
    out << std::string(position, ' ') << "</document>" << std::endl;
}

// Реализация draw для всех типов, которые можно передать в ostream
template <typename T>
void draw(const T& object, std::ostream& out, size_t position) {
    out << std::string(position, ' ') << object << std::endl;
}

// ---------  user_code.hpp

#include <iostream>
#include "lib_doc.hpp"

using namespace std;

class myclass_t {};

// Реализация draw для myclass_t
void draw(const myclass_t &, std::ostream &out, size_t position) {
    out << std::string(position, ' ') << "myclass_t" << std::endl;
}

int main(int, char**) {
    document_t document;
    document.reserve(5);

    document.emplace_back(0);
    document.emplace_back(std::string("Hello"));
    document.emplace_back(document);
    document.emplace_back(myclass_t{});

    draw(document, std::cout, 0);
    return 0;
}
```