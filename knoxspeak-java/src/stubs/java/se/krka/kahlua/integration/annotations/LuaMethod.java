// automatically generated from projectzomboid.jar (sha256 3c4a7c5f149d), ZombieBuddy.jar (sha256 eb79b9876332).
// do not edit
package se.krka.kahlua.integration.annotations;

@java.lang.annotation.Retention(java.lang.annotation.RetentionPolicy.RUNTIME)
@java.lang.annotation.Target({java.lang.annotation.ElementType.METHOD})
@java.lang.annotation.Inherited
public @interface LuaMethod {
    boolean global() default false;
    java.lang.String name() default "";
}
