---
title: "Линкуем C++ либы без конфликтов"
categories:
  - Article
tags:
  - programming
  - C
  - C++
  - linux
  - cmake
  - linker
classes: wide
---
Порой в одном приложении нужны две версии одной и той же библиотеки - и это приводит к неожиданным конфликтам символов при линковке. Расскажу, как с этим справиться.

# Постановка задачи
Основное приложение `main_app` линкуется со статической библиотекой `libstatic_v2.a`. Для новой фичи подключить стороннюю библиотеку `libdynamic.so`, которая зависима от `libshared_v1.so` - по сути того же кода из `libstatic_v2.a`, но старой версии.  

Понизить версию `libstatic_v2.a` нельзя - проект может сломаться, а чужую библиотеку переделывать никто не будет. При этом в интерфейсах `libdynamic.so` ничего из `libshared_v1.so` нет, так что две библиотеки, в теории, могут жить рядом.  

Разберемся, что делать, если один и тот же символ реализован дважды, и линковщик "не понимает", какую версию выбрать.
## Демонстрационный hello_world
> Демо лежит на [Godbolt](https://godbolt.org/z/zocGTTWjn), где можно управлять поведением через cmake флаги. Весь код проекта на [GitHub](https://github.com/XCemaXX/symbol-clash-demo) 

Код заголовочных файлов `libstatic_v2.a` и `libshared_v1.so` идентичен, но у разных версий реализация отличается:
```cpp
// header_v1.h header_v2.h
void print_version();
// main_v1.cpp
void print_version() {
    std::cout << "1" << std::endl;
}
// main_v2.cpp
void print_version() {
    std::cout << "2" << std::endl;
}
```
В `libdynamic.so` реализована обертка над `print_version` из `libshared_v1.so`:
```cpp
#include "header_v1.h"
extern void print_version();
void call_print_from_v1() { 
    std::cout << "Call to v1: ";
    print_version(); 
}
```
В `main_app` находятся два вызова из статической и динамической библиотек:
```cpp
#include "header_v2.h"
extern void print_version();         // from static lib_v2
extern void call_print_from_v1();    // from dynamic libdyn
int main() {
    std::cout << "Static lib_v2 prints: ";
    print_version();
    std::cout << "Dynamic libdyn->lib_v1 prints: ";
    call_print_from_v1();
}
```
Собираем все через Cmake:
```cmake
add_library(lib_v1_shared SHARED main_v1.cpp)
set_target_properties(lib_v1_shared PROPERTIES OUTPUT_NAME shared_v1)

add_library(lib_v2_static STATIC main_v2.cpp)
set_target_properties(lib_v2_static PROPERTIES OUTPUT_NAME static_v2)

add_library(lib_dyn SHARED lib_dyn.cpp)
set_target_properties(lib_dyn PROPERTIES OUTPUT_NAME dynamic)
target_link_libraries(lib_dyn PRIVATE lib_v1_shared)

add_executable(main_app main.cpp)
target_link_libraries(main_app PRIVATE lib_v2_static lib_dyn)
```

# Конфликт
После запуска ожидаем увидеть корректную работу:
```
Static lib_v2 prints: 2
Dynamic libdyn->lib_v1 prints: Call to v1: 2 // <----- ожидали 1
```
Линковщик берёт первый **глобальный** символ `print_version` из статической библиотеки и отдаёт его `libdynamic.so`.
## Скрываем символы
Решение заключается в том, чтобы символы из статической библиотеки убрать из глобальной области видимости. Для этого есть два способа.  
### Способ первый: exclude-libs
Указываем во время компановки проекта `main_app`, что нужно скрыть символы из зависимостей:
```cmake
# всех библиотек
target_link_options(main_app PRIVATE "-Wl,--exclude-libs,ALL")
# конкретной библиотеки
target_link_options(main_app PRIVATE "-Wl,--exclude-libs,libstatic_v2.a")
```
Способ работает только в GNU. 
### Способ второй: visibility=hidden
Этот способ универсален, указывается во время компиляции статической библиотеки:
```cmake
target_compile_options(lib_v2_static PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
```
## Проверка
После реализации одного из способов выше обе версии `print_version` резолвятся корректно:
```
Static lib_v2 prints: 2
Dynamic libdyn->lib_v1 prints: Call to v1: 1
```
Убедимся в корректности исправлений с помощью `readelf`, `nm` и `LD_DEBUG`.  

Посмотрим, что `libdynamic.so` будет пытаться резолвить символ:
```bash
readelf --dyn-syms lib/libdynamic.so | grep print_version
     6: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND _Z13print_versionv
# или
nm lib/libdynamic.so | grep print_version
                 U _Z13print_versionv
```
Флаг `UND (U)` говорит о том, что реализация этой функции будет взята из внешнего источника.  

До добавления исправляющих флагов сборки/компоновки символ `print_version` должен быть глобальным:
```bash
readelf --dyn-syms main_app | grep print_version
    17: 0000000000001300    40 FUNC    GLOBAL DEFAULT   15 _Z13print_versionv
# или
nm main_app | grep print_version
0000000000001300 T _Z13print_versionv

LD_DEBUG=bindings main_app 2>&1 | grep print_version
    944267:     binding file libdynamic.so [0] to main_app [0]: normal symbol `_Z13print_versionv
```
В выводе первых двух команд видим флаг `GLOBAL (T)`, значащий, что символ глобальный. Третья команда показывает, что символ `print_version` для `libdynamic.so` берется из `main_app`.  

Посмотрим на вывод аналогичных команд для версии проекта без конфликта:
```bash
readelf --dyn-syms main_app | grep print_version
# пусто
nm main_app | grep print_version
00000000000012a0 t _Z13print_versionv

LD_DEBUG=bindings main_app 2>&1 | grep print_version 
    945221:     binding file libdynamic.so [0] to libshared_v1.so [0]: normal symbol `_Z13print_versionv'
```
Теперь флаг `t` в `nm` показывает, что символ локальный. Символ `print_version` для `libdynamic.so` берется из `libshared_v1.so`.

# Символы шаблонов
С обычными функциями проблема решена. В C++ в интерфейсах библиотек могут быть шаблоны, которые добавляют новый уровень боли.
## Шаблоны в hello_world
Добавим шаблонную функцию с одинаковой сигнатурой, но разной реализацией в заголовочные файлы `libstatic_v2.a` и `libshared_v1.so`. Добавим обертку в `libdynamic.so` и соответствующие вызовы в `main_app`:
```cpp
// header_v1.h 
template<typename T> T multiply(T a, T b) { return a * b; }
// header_v2.h
template<typename T> T multiply(T a, T b) { return a * b + 200000; }
// libdynamic.so
int call_multiply_from_v1(int a, int b) {
    std::cout << "Call to v1: ";
    return multiply<int>(a, b);
}
// mani_app
int call_multiply_from_v1(int,int);
int main() {
  // ...
  std::cout << "multiply<int>() direct from v2: " << multiply<int>(3,4) << std::endl;
  std::cout << "call_multiply_from_v1: " << call_multiply_from_v1(3,4) << std::endl;
}
```
## Конфликт
Собираем проект и ожидаем корректный вывод:
```
multiply<int>() direct from v2: 200012
call_multiply_from_v1: Call to v1: 200012 // <----- ожидали 12
```
Наличие флагов `-fvisibility=hidden`, `-Wl,--exclude-libs,ALL` ситуаюцию не исправляет.  

Попробуем закоментировать первую строку с вызовом `multiply<int>(3,4)`:
```
call_multiply_from_v1: Call to v1: 12
```
Удивительно, но теперь получили верный ответ.  

Разгадка кроется в месте инстанцирования шаблонов: шаблоны истанцируются в тех единицах трасляции, где были вызваны. Т.е. вызов `multiply<int>(3,4)` инстанцирует шаблон в `main_app`, создавая глобальный символ. Линковщик берёт **глобальный** символ `multiply<int>` и подставляет его `libdynamic.so`, хотя в этой библиотеке он есть:
```bash
nm libdynamic.so | grep multiply
00000000000011f0 W _Z8multiplyIiET_S0_S0_
nm main_app | grep mult
00000000000012f0 W _Z8multiplyIiET_S0_S0_
```
Символ помечен `W` - weak. Так как оба типа равнозначны, линковщик берет символ из `main_app`.
## Скрываем символы v2
Решение заключается в том, чтобы символы из `main_app` убрать из глобальной области видимости.  

В этот раз нам нужно будет создать дополнительный файл со списком глобальных и локальных символов и добавить в флаги компановщика:
```
# main_app.map
{
  global:
    main;
  local:
    *;
};
# cmake main_app
target_link_options(main_app PRIVATE "-Wl,--version-script=${CMAKE_SOURCE_DIR}/main_app.map")
```
Метод работает только для GNU.
## Проверка
После подключения к проекту `main_app.map` получим полностью рабочий пример. При этом скрытие символов от статической библиотеки больше не нужно.
```
Static lib_v2 prints: 2
Dynamic libdyn->lib_v1 prints: Call to v1: 1
multiply<int>() direct from v2: 200012
call_multiply_from_v1: Call to v1: 12
```
Проверим, что все действительно так:
```bash
LD_DEBUG=bindings main_app 2>&1 | grep multiply
# до исправления 
944647:     binding file libdynamic.so [0] to main_app [0]: normal symbol `_Z8multiplyIiET_S0_S0_' 
# после
944739:     binding file libdynamic.so [0] to libdynamic.so [0]: normal symbol `_Z8multiplyIiET_S0_S0_' 
```
# Полный пример и сборка
Демо-проект лежит на [GitHub](https://github.com/XCemaXX/symbol-clash-demo).  

Сборка со всеми опциями:
```bash
cmake -S . -B build -DWITH_VISIBILITY_HIDDEN=ON/OFF \
         -DWITH_EXCLUDE_LIB2=ON/OFF \
         -DWITH_VERSION_SCRIPT=ON/OFF \
         -DCOMMENT_TEMPLATE_STATIC_CALL=0/1
cmake --build build
./build/bin/main_app
```
# Вывод
Линковка - дело тонкое. Я уже дважды сталкивался с её подводными камнями: сначала - с настройкой `rpath` для плагина Wireshark, теперь - с управлением видимостью символов через `visibility`, `--exclude-libs` и `version scripts`. Оба опыта показали, что конфликты получить крайне легко.