---
layout: post
title: "Formatting in place"
tags: [markdown, test]
---

# A first heading

Plain text, then *italic*, **bold** and ***both***. A snake_case_name
stays plain, and so does 2 * 3 * 4.

## Code and links

Inline `code *not italic*` and a [link to a page](https://example.com/a_b).
An image: ![a small picture](/assets/images/posts/test/pic.jpg) and a
reference [link][ref], a footnote[^1], <https://example.com> and \*escaped\*.

```c
# not a heading
int *p = NULL;
```

### Lists and quotes

- one item with **bold**
- [x] a task
1. a numbered item

> A quotation with *emphasis*.

{% include note.html %} and <br/> stay as they are.

---

[ref]: https://example.com/ref "Reference"
[^1]: The footnote.
