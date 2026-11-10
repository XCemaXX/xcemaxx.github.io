# The first image is often above the fold; lazy loading it delays the main paint.
Jekyll::Hooks.register :posts, :post_convert do |post|
  seen = 0
  post.content = post.content.gsub(/<img(?![^>]*\bloading=)/) do |tag|
    seen += 1
    seen == 1 ? tag : '<img loading="lazy"'
  end
end
