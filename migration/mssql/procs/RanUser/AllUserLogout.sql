create   PROCEDURE [dbo].[AllUserLogout]
as
begin	
	UPDATE UserInfo
	SET UserLoginState=0 
end
