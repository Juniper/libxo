<?xml version="1.0"?>
<!--
  Evaluate the expressions listed in the "exprs" document against the
  input document and print each result as xpath_03.test does, so the
  two outputs can be diffed.
-->
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:dyn="http://exslt.org/dynamic"
    xmlns:exsl="http://exslt.org/common"
    extension-element-prefixes="dyn exsl">
  <xsl:output method="text"/>
  <xsl:param name="exprs"/>
  <xsl:variable name="nl" select="'&#10;'"/>

  <xsl:template match="/">
    <xsl:variable name="root" select="/"/>
    <xsl:for-each select="document($exprs)/exprs/e">
      <xsl:variable name="expr" select="string(.)"/>
      <xsl:for-each select="$root">
        <xsl:call-template name="one">
          <xsl:with-param name="expr" select="$expr"/>
        </xsl:call-template>
      </xsl:for-each>
    </xsl:for-each>
  </xsl:template>

  <xsl:template name="one">
    <xsl:param name="expr"/>
    <xsl:variable name="res" select="dyn:evaluate($expr)"/>
    <xsl:variable name="type" select="exsl:object-type($res)"/>
    <xsl:value-of select="concat('expr: ', $expr, $nl)"/>
    <xsl:choose>
      <xsl:when test="$type = 'node-set'">
        <xsl:value-of select="concat('node-set ', count($res), $nl)"/>
        <xsl:for-each select="$res">
          <xsl:variable name="kind">
            <xsl:choose>
              <xsl:when test="not(parent::node())">root</xsl:when>
              <xsl:when test="self::*">element</xsl:when>
              <xsl:when test="self::text()">text</xsl:when>
              <xsl:otherwise>attribute</xsl:otherwise>
            </xsl:choose>
          </xsl:variable>
          <xsl:value-of select="concat('  ', position(), ': ', $kind, ' ',
                                name(), ' &quot;',
                                translate(string(.), $nl, ' '),
                                '&quot;', $nl)"/>
        </xsl:for-each>
      </xsl:when>
      <xsl:when test="$type = 'boolean'">
        <xsl:value-of select="concat('boolean ', string($res), $nl)"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:value-of select="concat('value &quot;',
                              translate(string($res), $nl, ' '),
                              '&quot;', $nl)"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>
</xsl:stylesheet>
