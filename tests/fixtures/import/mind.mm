<?xml version="1.0" encoding="UTF-8"?>
<map version="1.0.1">
  <node ID="top">
    <richcontent TYPE="NODE"><html><body><p>章节 <b>复习</b></p></body></html></richcontent>
    <richcontent TYPE="NOTE"><html><head><title>不属于正文</title></head><body>
      <p>备注第一行<br/>下一行</p>
      <p>第二段 &amp; 样例</p>
    </body></html></richcontent>
    <node ID="right" TEXT="右侧" POSITION="right" LINK="https://example.org/page">
      <arrowlink DESTINATION="left"/>
    </node>
    <node ID="left" TEXT="左侧" POSITION="left" FOLDED="true">
      <node ID="detail" TEXT="折叠细节"/>
    </node>
  </node>
</map>
