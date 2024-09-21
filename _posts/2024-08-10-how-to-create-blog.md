---
title: "Как сделать блог"
categories:
  - Instructions
tags:
  - instruction
  - blog
classes: wide
---

# Зачем блог  
[О пользе ведения профессионального блога](https://vsevolodustinov.ru/blog/all/o-polze-vedeniya-professionalnogo-bloga/)  

[Зачем вести блог (несмотря ни на что)](https://sergeykorol.ru/blog/about-blog/)  
# До блога  
[Начал с постов в linkedin](https://www.linkedin.com/in/xcemaxx/recent-activity/all/)  

[Потом сделал пару постов на vas3k](/third_party/)  

[Завел спортивный канал в телеграмме](https://t.me/const_gym)  

# Техническая реализация
[Смотрим в инструкцию и выполняем, что написано](https://github.com/barryclark/jekyll-now)

## Качаем нужный шаблон
Сначала качаем репозиторий себе внутрь github.io репы  
```
git clone https://github.com/barryclark/jekyll-now.git
```  
Правим _config.yml

## Устанавливаем ruby и jekyll
[Инструкция](https://jekyllrb.com/docs/installation/ubuntu/)  
```
sudo apt update
sudo apt install ruby-full build-essential zlib1g-dev
echo '# Install Ruby Gems to ~/gems' >> ~/.bashrc
echo 'export GEM_HOME="$HOME/gems"' >> ~/.bashrc
echo 'export PATH="$HOME/gems/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
gem install jekyll bundler
gem install github-pages
```  
## Запускаем свой сайт 
Команда для запуска  
```
rm -rf _site; rm -rf .jekyll-cache; rm -rf .bundle; jekyll serve
```  
Проверяем на `http://127.0.0.1:4000/`  
## Подстраиваем  
Смотрим [пример запущенного шаблона с разными плюшками](https://mmistakes.github.io/minimal-mistakes/docs/quick-start-guide/)  

[Полный код проекта](https://github.com/mmistakes/minimal-mistakes/tree/master/docs)