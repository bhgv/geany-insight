/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "document.h"
static char *bytes(JNIEnv *env,jbyteArray array,jsize limit,jsize *length) {
 if(!array) return NULL;
 *length=(*env)->GetArrayLength(env,array);
 if(*length>limit) return NULL;
 char *copy=malloc((size_t)*length+1); if(!copy) return NULL;
 (*env)->GetByteArrayRegion(env,array,0,*length,(jbyte *)copy);
 if((*env)->ExceptionCheck(env)) { free(copy); return NULL; }
 copy[*length]=0; return copy;
}
static jbyteArray array(JNIEnv *env,const char *s,size_t length) {
 jbyteArray out=(*env)->NewByteArray(env,(jsize)length);
 if(out) (*env)->SetByteArrayRegion(env,out,0,(jsize)length,(const jbyte *)s);
 return out;
}
JNIEXPORT jobject JNICALL Java_org_geany_insight_NativeBridge_run(
 JNIEnv *env,jclass klass,jbyteArray input,jint start,jint end,jbyteArray script) {
 (void)klass; jsize text_length=0,script_length=0;
 char *text=bytes(env,input,INSIGHT_DOCUMENT_LIMIT,&text_length);
 char *source=bytes(env,script,256*1024,&script_length);
 InsightDocument doc={0}; char error[512]="";
 int ok=0;
 if((*env)->ExceptionCheck(env)) goto cleanup;
 if(!text || !source) strcpy(error,"Missing input, allocation failure, or size limit exceeded");
 else if(start<0 || end<start || !insight_document_init(&doc,text,(size_t)text_length,(size_t)start,(size_t)end))
  strcpy(error,"Invalid UTF-8 document or selection");
 else {
  InsightHost host={.context=&doc,.text=insight_document_text,.selection=insight_document_selection,.replace_selection=insight_document_replace,.index=NULL};
  ok=insight_run(&host,source,(size_t)script_length,error,sizeof error)==0;
  if(!ok && !error[0]) strcpy(error,"Lua runtime unavailable");
 }
 /* Byte arrays avoid JNI Modified UTF-8, including emoji and non-ASCII text. */
 jclass result_class=(*env)->FindClass(env,"org/geany/insight/NativeBridge$Result");
 if(!result_class) goto cleanup;
 jmethodID constructor=(*env)->GetMethodID(env,result_class,"<init>","([B[BII)V");
 if(!constructor) goto cleanup;
 jbyteArray output=ok ? array(env,doc.text,doc.length) : input;
 if((*env)->ExceptionCheck(env)) goto cleanup;
 jbyteArray message=array(env,error,strlen(error));
 if(!message || (*env)->ExceptionCheck(env)) goto cleanup;
 jobject result=(*env)->NewObject(env,result_class,constructor,output,message,
  ok ? (jint)doc.start : start,ok ? (jint)doc.end : end);
 insight_document_dispose(&doc); free(source); free(text); return result;
cleanup:
 insight_document_dispose(&doc); free(source); free(text); return NULL;
}
