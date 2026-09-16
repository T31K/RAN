
CREATE  PROCEDURE [dbo].[TICT_GetUserNum]
(
	@ChaName varchar(33),
	@UserNum int output
)
AS
begin
	select @UserNum = usernum from chainfo where chaname=@ChaName
	if @@rowcount=0
	 set @UserNum =0
end

