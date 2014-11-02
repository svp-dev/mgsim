// -*- c++ -*-

{% macro expand_members(items) %}
 {%- for item in items.state %}
  {%- if item.0 == 'union' %}
   union {
       {{ expand_members(item) }}
   };
  {%- elif item.0 == 'struct' %}
   struct {
       {{ expand_members(item) }}
   };
  {%- elif item.0 == 'array' %}
    {{ item.2 }} {{ item.1 }}[ {{ item.3 }}];
  {%- elif item.0 == 'raw' %}
   {%- for r in item.1 %}
      {{ r }}
   {%- endfor %}
  {%- else %}
   {{ item.0 }} {{ item.1 }};
  {%- endif %}
 {%- endfor %}
{%- endmacro %}

{% macro gen_serializer(items) -%}
{%- set members = items.state|rejectattr('noserialize')|list -%}
SERIALIZE(__a) {
    __a & "[{{ members|map(attribute=1)|join(' ')|chksum }}";
  {%- for v in members %}
  {%- if v.0 == 'array' %}
     {%- if v.2 == 'char' %}
     __a & Serialization::binary({{ v.1 }}, {{ v.3 }});
     {%- elif v.2 == 'bool' %}
     __a & Serialization::bitvec({{ v.1 }}, {{ v.3 }});
     {%- else %}
     for (auto& __v : {{ v.1 }}) __a & __v;
     {%- endif %}
  {%- else %}
     __a & {{ v.1 }};
  {%- endif %}
  {%- endfor %}
  {%- if items.serializer_append %}
  {{ items.serializer_append.0 }}
  {%- endif %}
  __a & "]";
}
{%- endmacro %}

// gen_variant: generate a variant type
{% macro gen_variant() %}
// ***** GENERATED CODE - DO NOT MODIFY  *****
{% set md = from_sexp(caller()) %}
struct {{ md.name.0 }}
{
 enum Type
 {
  {%- for item in md.variants %}
    {{ item.0 }}{{ ',' if not loop.last }}
  {%- endfor %}
 };
 Type type;

 union
 {
  {%- for item in md.variants if item.1 is defined %}
  struct {
      {{ expand_members(item) }}
      {{ gen_serializer(item) }}
  } {{ item.1 }};
  {%- endfor %}
 };

 SERIALIZE(__a) {
  __a & type;
  switch(type) {
  {%- for item in md.variants %}
  case {{ item.0 }}: {%- if item.1 is defined -%} __a & {{ item.1 }}; {%- endif -%} break;
  {%- endfor %}
  }
 }

 {%- if md.raw %}
   {%- for r in md.raw %}
     {{ r }}
   {% endfor -%}
 {%- endif %}
};
// ***** END GENERATED CODE *****
{% endmacro %}

// gen_struct: generate a "plain" C-like struct.
//
{% macro gen_struct() %}
// ***** GENERATED CODE - DO NOT MODIFY  *****
{% set md = from_sexp(caller()) %}
struct {{ md.name.0 }}
   {%- if md.inherits is defined -%}
    : {{ md.inherits | join(',') }}
   {%- endif %}
{
    {{ expand_members(md) }}
    {{ gen_serializer(md) }}

    {% set init_members = md.state|selectattr('init')|list %}
    {%- if init_members|count > 0 %}

    {%- if init_members|map(attribute='nocopy')|list != init_members %}
    {{ md.name.0 }} (
        {%- for item in init_members if item.nocopy is undefined %}
         {{ item.0 }} _{{ item.1 }}{{ ', ' if not loop.last }}
        {%- endfor -%}
        ) :
    {%- for item in init_members if item.0 != 'array' %}
      {{ item.1 }}(
          {%- if item.nocopy is defined -%}
             {{ item.init|join(',') if item.init is defined }}
          {%- else -%}
             _{{ item. 1}}
          {%- endif -%}
          ){{ ', ' if not loop.last }}
    {%- endfor -%}
    {}
    {%- endif %}

    {{ md.name.0 }}():
    {%- for item in init_members %}
      {{ item.1 }}( {{ item.init|join(',') }} ) {{ ',' if not loop.last }}
    {%- endfor -%}
    {}

    {%- endif %}
};
// ***** END GENERATED CODE *****
{% endmacro %}
